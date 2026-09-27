#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "WindowSystem.h"
#include "Cemu/Logging/CemuLogging.h"

performanceMonitor_t performanceMonitor{};

namespace
{
constexpr uint32 kTelemetryLogIntervalMs = 2000;
uint32 s_lastTelemetryLog = 0;

double TimerValueToMilliseconds(LattePerfStatTimer& timer)
{
	return static_cast<double>(PPCTimer_tscToMicroseconds(timer.getPreviousFrameValue())) / 1000.0;
}
}

void LattePerformanceMonitor_frameEnd()
{
	// per-frame stats
	performanceMonitor.gpuTime_shaderCreate.frameFinished();
	performanceMonitor.gpuTime_frameTime.frameFinished();
	performanceMonitor.gpuTime_idleTime.frameFinished();
	performanceMonitor.gpuTime_fenceTime.frameFinished();

	performanceMonitor.gpuTime_dcStageTextures.frameFinished();
	performanceMonitor.gpuTime_dcStageVertexMgr.frameFinished();
	performanceMonitor.gpuTime_dcStageShaderAndUniformMgr.frameFinished();
	performanceMonitor.gpuTime_dcStageIndexMgr.frameFinished();
	performanceMonitor.gpuTime_dcStageMRT.frameFinished();
	performanceMonitor.gpuTime_dcStageDrawcallAPI.frameFinished();
	performanceMonitor.gpuTime_waitForAsync.frameFinished();
	performanceMonitor.vk.vulkanDrawSequenceBeginTime.frameFinished();
	performanceMonitor.vk.vulkanPipelineCacheQueryTime.frameFinished();
	performanceMonitor.vk.vulkanPipelineBindTime.frameFinished();

	uint32 elapsedTime = GetTickCount() - performanceMonitor.cycle[performanceMonitor.cycleIndex].lastUpdate;
	if (elapsedTime >= 1000)
	{
		bool isFirstUpdate = performanceMonitor.cycle[performanceMonitor.cycleIndex].lastUpdate == 0;
		// sum up raw stats
		uint32 totalElapsedTime = GetTickCount() - performanceMonitor.cycle[(performanceMonitor.cycleIndex + 1) % PERFORMANCE_MONITOR_TRACK_CYCLES].lastUpdate;
		uint32 totalElapsedTimeFPS = GetTickCount() - performanceMonitor.cycle[(performanceMonitor.cycleIndex + PERFORMANCE_MONITOR_TRACK_CYCLES - 1) % PERFORMANCE_MONITOR_TRACK_CYCLES].lastUpdate;
		uint32 elapsedFrames = 0;
		uint32 elapsedFrames2S = 0; // elapsed frames for last two entries (seconds)
		uint64 skippedCycles = 0;
		uint64 vertexDataUploaded = 0;
		uint64 vertexDataCached = 0;
		uint64 uniformBankUploadedData = 0;
		uint64 uniformBankUploadedCount = 0;
		uint64 indexDataUploaded = 0;
		uint64 indexDataCached = 0;
		uint32 frameCounter = 0;
		uint32 drawCallCounter = 0;
		uint32 fastDrawCallCounter = 0;
		uint32 shaderBindCounter = 0;
		uint32 recompilerLeaveCount = 0;
		uint32 threadLeaveCount = 0;
		for (sint32 i = 0; i < PERFORMANCE_MONITOR_TRACK_CYCLES; i++)
		{
			elapsedFrames += performanceMonitor.cycle[i].frameCounter;
			skippedCycles += performanceMonitor.cycle[i].skippedCycles;
			vertexDataUploaded += performanceMonitor.cycle[i].vertexDataUploaded;
			vertexDataCached += performanceMonitor.cycle[i].vertexDataCached;
			uniformBankUploadedData += performanceMonitor.cycle[i].uniformBankUploadedData;
			uniformBankUploadedCount += performanceMonitor.cycle[i].uniformBankUploadedCount;
			indexDataUploaded += performanceMonitor.cycle[i].indexDataUploaded;
			indexDataCached += performanceMonitor.cycle[i].indexDataCached;
			frameCounter += performanceMonitor.cycle[i].frameCounter;
			drawCallCounter += performanceMonitor.cycle[i].drawCallCounter;
			fastDrawCallCounter += performanceMonitor.cycle[i].fastDrawCallCounter;
			shaderBindCounter += performanceMonitor.cycle[i].shaderBindCount;
			recompilerLeaveCount += performanceMonitor.cycle[i].recompilerLeaveCount;
			threadLeaveCount += performanceMonitor.cycle[i].threadLeaveCount;
		}
		elapsedFrames = std::max<uint32>(elapsedFrames, 1);
		elapsedFrames2S = performanceMonitor.cycle[(performanceMonitor.cycleIndex + PERFORMANCE_MONITOR_TRACK_CYCLES - 0) % PERFORMANCE_MONITOR_TRACK_CYCLES].frameCounter;
		elapsedFrames2S += performanceMonitor.cycle[(performanceMonitor.cycleIndex + PERFORMANCE_MONITOR_TRACK_CYCLES - 1) % PERFORMANCE_MONITOR_TRACK_CYCLES].frameCounter;
		elapsedFrames2S = std::max<uint32>(elapsedFrames2S, 1);
		// calculate stats
		uint64 passedCycles = PPCInterpreter_getMainCoreCycleCounter() - performanceMonitor.cycle[(performanceMonitor.cycleIndex + 1) % PERFORMANCE_MONITOR_TRACK_CYCLES].lastCycleCount;
		passedCycles -= skippedCycles;
		uint64 vertexDataUploadPerFrame = (vertexDataUploaded / (uint64)elapsedFrames);
		vertexDataUploadPerFrame /= 1024ULL;
		uint64 vertexDataCachedPerFrame = (vertexDataCached / (uint64)elapsedFrames);
		vertexDataCachedPerFrame /= 1024ULL;
		uint64 uniformBankDataUploadedPerFrame = (uniformBankUploadedData / (uint64)elapsedFrames);
		uniformBankDataUploadedPerFrame /= 1024ULL;
		uint32 uniformBankCountUploadedPerFrame = (uint32)(uniformBankUploadedCount / (uint64)elapsedFrames);
		uint64 indexDataUploadPerFrame = (indexDataUploaded / (uint64)elapsedFrames);

		double fps = (double)elapsedFrames2S * 1000.0 / (double)totalElapsedTimeFPS;
		uint32 shaderBindsPerFrame = shaderBindCounter / elapsedFrames;
		passedCycles = passedCycles * 1000ULL / totalElapsedTime;
		uint32 rlps = (uint32)((uint64)recompilerLeaveCount * 1000ULL / (uint64)totalElapsedTime);
		uint32 tlps = (uint32)((uint64)threadLeaveCount * 1000ULL / (uint64)totalElapsedTime);
		// set stats
		performanceMonitor.stats.indexDataUploadPerFrame = indexDataUploadPerFrame;
		// next counter cycle
		sint32 nextCycleIndex = (performanceMonitor.cycleIndex + 1) % PERFORMANCE_MONITOR_TRACK_CYCLES;
		performanceMonitor.cycle[nextCycleIndex].drawCallCounter = 0;
		performanceMonitor.cycle[nextCycleIndex].fastDrawCallCounter = 0;
		performanceMonitor.cycle[nextCycleIndex].frameCounter = 0;
		performanceMonitor.cycle[nextCycleIndex].shaderBindCount = 0;
		performanceMonitor.cycle[nextCycleIndex].lastCycleCount = PPCInterpreter_getMainCoreCycleCounter();
		performanceMonitor.cycle[nextCycleIndex].skippedCycles = 0;
		performanceMonitor.cycle[nextCycleIndex].vertexDataUploaded = 0;
		performanceMonitor.cycle[nextCycleIndex].vertexDataCached = 0;
		performanceMonitor.cycle[nextCycleIndex].uniformBankUploadedData = 0;
		performanceMonitor.cycle[nextCycleIndex].uniformBankUploadedCount = 0;
		performanceMonitor.cycle[nextCycleIndex].indexDataUploaded = 0;
		performanceMonitor.cycle[nextCycleIndex].indexDataCached = 0;
		performanceMonitor.cycle[nextCycleIndex].recompilerLeaveCount = 0;
		performanceMonitor.cycle[nextCycleIndex].threadLeaveCount = 0;
		performanceMonitor.cycleIndex = nextCycleIndex;

		// next update in 1 second
		performanceMonitor.cycle[performanceMonitor.cycleIndex].lastUpdate = GetTickCount();

		if (isFirstUpdate)
		{
			LatteOverlay_updateStats(0.0, 0, 0);
			WindowSystem::UpdateWindowTitles(false, false, 0.0);
		}
		else
		{
			const uint32 drawCallsPerFrame = drawCallCounter / elapsedFrames;
			const uint32 fastDrawCallsPerFrame = fastDrawCallCounter / elapsedFrames;
			LatteOverlay_updateStats(fps, drawCallsPerFrame, fastDrawCallsPerFrame);
			WindowSystem::UpdateWindowTitles(false, false, fps);
			const uint32 now = GetTickCount();
			if (now - s_lastTelemetryLog >= kTelemetryLogIntervalMs)
			{
				s_lastTelemetryLog = now;
				cemuLog_log(LogType::Force,
					"Cemu performance telemetry: fps={:.2f} drawCallsPerFrame={} fastDrawCallsPerFrame={} renderCpuMs={:.3f} fenceWaitMs={:.3f} asyncWaitMs={:.3f} shaderCreateMs={:.3f}",
					fps, drawCallsPerFrame, fastDrawCallsPerFrame,
					TimerValueToMilliseconds(performanceMonitor.gpuTime_frameTime),
					TimerValueToMilliseconds(performanceMonitor.gpuTime_fenceTime),
					TimerValueToMilliseconds(performanceMonitor.gpuTime_waitForAsync),
					TimerValueToMilliseconds(performanceMonitor.gpuTime_shaderCreate));
			}
			if (fps < 28.0)
			{
				cemuLog_log(LogType::Force,
					"Cemu performance drop: fps={:.2f} drawCallsPerFrame={} fastDrawCallsPerFrame={} renderCpuMs={:.3f} recompilerLeavesPerSecond={} threadLeavesPerSecond={} indexUploadKiBPerFrame={} vkPipelines={} vkDescriptorSets={} vkImages={} vkImageViews={} vkRenderPasses={} vkFramebuffers={} barriersLastFrame={} inputTextureBarriersLastFrame={} renderPassLoadBarriersLastFrame={} skippedColorFeedbackBarriersLastFrame={} beginRenderPassesLastFrame={} renderPassFboChangesLastFrame={} renderPassSelfDependencySplitsLastFrame={} renderPassReopensSameFboLastFrame={} bufferCache=[initialUploads:{},changedUploads:{},streamoutUploads:{},uploadKiB:{},pagesChecked:{},pagesChanged:{}] uploadSources=[vertex:{}/{},vsUniform:{}/{},gsUniform:{}/{},psUniform:{}/{},directVertex:{}/{}] directVertexClassifier=[probes:{},changes:{},promotions:{},demotions:{}] directVertexEligibility=[smallRequests:{},historyMisses:{},cacheHits:{},learningUploads:{},sameFrameUploads:{},ringRejects:{},oversizedUploads:{},oversizedKiB:{},historyResets:{}] directVertexPromotionSizes=[le256:{},le512:{},le1024:{},le2048:{},le4096:{}] directVertexBindSizes=[le256:{},le512:{},le1024:{},le2048:{},le4096:{}] fastDrawPassEnds=[streamout:{},queueEmpty:{},textureChange:{},contextChange:{},samplerChange:{},unsupportedType3:{},unsupportedPacket:{}] renderPassEndsLastFrame=[submit:{},present:{},clear:{},textureTransfer:{},bufferTransfer:{},query:{},readback:{},fboTransition:{},other:{}]",
					fps, drawCallsPerFrame, fastDrawCallsPerFrame,
					TimerValueToMilliseconds(performanceMonitor.gpuTime_frameTime), rlps, tlps,
					(performanceMonitor.stats.indexDataUploadPerFrame + 1023) / 1024,
					performanceMonitor.vk.numGraphicPipelines.get(),
					performanceMonitor.vk.numDescriptorSets.get(),
					performanceMonitor.vk.numImages.get(),
					performanceMonitor.vk.numImageViews.get(),
					performanceMonitor.vk.numRenderPass.get(),
					performanceMonitor.vk.numFramebuffer.get(),
					performanceMonitor.vk.numDrawBarriersPerFrame.get(),
					performanceMonitor.vk.numInputTextureBarriersPerFrame.get(),
					performanceMonitor.vk.numRenderPassLoadBarriersPerFrame.get(),
					performanceMonitor.vk.numSkippedColorFeedbackBarriersPerFrame.get(),
					performanceMonitor.vk.numBeginRenderpassPerFrame.get(),
					performanceMonitor.vk.numRenderPassFboChangesPerFrame.get(),
					performanceMonitor.vk.numRenderPassSelfDependencySplitsPerFrame.get(),
					performanceMonitor.vk.numRenderPassReopensSameFboPerFrame.get(),
					performanceMonitor.vk.numBufferCacheInitialUploadsPerFrame.get(),
					performanceMonitor.vk.numBufferCacheChangedUploadsPerFrame.get(),
					performanceMonitor.vk.numBufferCacheStreamoutUploadsPerFrame.get(),
					(performanceMonitor.vk.numBufferCacheUploadBytesPerFrame.get() + 1023) / 1024,
					performanceMonitor.vk.numBufferCachePagesCheckedPerFrame.get(),
					performanceMonitor.vk.numBufferCachePagesChangedPerFrame.get(),
					performanceMonitor.vk.numBufferCacheVertexUploadsPerFrame.get(),
					(performanceMonitor.vk.numBufferCacheVertexUploadBytesPerFrame.get() + 1023) / 1024,
					performanceMonitor.vk.numBufferCacheVertexUniformUploadsPerFrame.get(),
					(performanceMonitor.vk.numBufferCacheVertexUniformUploadBytesPerFrame.get() + 1023) / 1024,
					performanceMonitor.vk.numBufferCacheGeometryUniformUploadsPerFrame.get(),
					(performanceMonitor.vk.numBufferCacheGeometryUniformUploadBytesPerFrame.get() + 1023) / 1024,
					performanceMonitor.vk.numBufferCachePixelUniformUploadsPerFrame.get(),
					(performanceMonitor.vk.numBufferCachePixelUniformUploadBytesPerFrame.get() + 1023) / 1024,
					performanceMonitor.vk.numDirectVertexUploadsPerFrame.get(),
					(performanceMonitor.vk.numDirectVertexUploadBytesPerFrame.get() + 1023) / 1024,
					performanceMonitor.vk.numDirectVertexProbesPerFrame.get(),
					performanceMonitor.vk.numDirectVertexChangesPerFrame.get(),
					performanceMonitor.vk.numDirectVertexPromotionsPerFrame.get(),
					performanceMonitor.vk.numDirectVertexDemotionsPerFrame.get(),
					performanceMonitor.vk.numDirectVertexSmallRequestsPerFrame.get(),
					performanceMonitor.vk.numDirectVertexHistoryMissesPerFrame.get(),
					performanceMonitor.vk.numDirectVertexCacheHitsPerFrame.get(),
					performanceMonitor.vk.numDirectVertexLearningUploadsPerFrame.get(),
					performanceMonitor.vk.numDirectVertexSameFrameUploadsPerFrame.get(),
					performanceMonitor.vk.numDirectVertexRingRejectsPerFrame.get(),
					performanceMonitor.vk.numDirectVertexOversizedUploadsPerFrame.get(),
					(performanceMonitor.vk.numDirectVertexOversizedUploadBytesPerFrame.get() + 1023) / 1024,
					performanceMonitor.vk.numDirectVertexHistoryResetsPerFrame.get(),
					performanceMonitor.vk.numDirectVertexPromotionsLe256PerFrame.get(),
					performanceMonitor.vk.numDirectVertexPromotionsLe512PerFrame.get(),
					performanceMonitor.vk.numDirectVertexPromotionsLe1024PerFrame.get(),
					performanceMonitor.vk.numDirectVertexPromotionsLe2048PerFrame.get(),
					performanceMonitor.vk.numDirectVertexPromotionsLe4096PerFrame.get(),
					performanceMonitor.vk.numDirectVertexBindsLe256PerFrame.get(),
					performanceMonitor.vk.numDirectVertexBindsLe512PerFrame.get(),
					performanceMonitor.vk.numDirectVertexBindsLe1024PerFrame.get(),
					performanceMonitor.vk.numDirectVertexBindsLe2048PerFrame.get(),
					performanceMonitor.vk.numDirectVertexBindsLe4096PerFrame.get(),
					performanceMonitor.vk.numFastDrawPassEndsStreamoutPerFrame.get(),
					performanceMonitor.vk.numFastDrawPassEndsQueueEmptyPerFrame.get(),
					performanceMonitor.vk.numFastDrawPassEndsTextureChangePerFrame.get(),
					performanceMonitor.vk.numFastDrawPassEndsContextChangePerFrame.get(),
					performanceMonitor.vk.numFastDrawPassEndsSamplerChangePerFrame.get(),
					performanceMonitor.vk.numFastDrawPassEndsUnsupportedType3PerFrame.get(),
					performanceMonitor.vk.numFastDrawPassEndsUnsupportedPacketPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsSubmitPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsPresentationPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsClearPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsTextureTransferPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsBufferTransferPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsQueryPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsReadbackPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsFboTransitionPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsOtherPerFrame.get());
				cemuLog_log(LogType::Force,
					"Cemu fast draw context buckets: a0xx={} a1xx={} a2xx={} a3xx={} a4xx={} a5xx={} a6xx={} a7xx={} a8xx={} a9xx={} aaxx={} abxx={} acxx={} adxx={} aexx={} afxx={}",
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[0].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[1].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[2].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[3].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[4].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[5].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[6].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[7].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[8].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[9].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[10].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[11].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[12].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[13].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[14].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame[15].get());
				cemuLog_log(LogType::Force,
					"Cemu fast draw context A2 buckets: a20x={} a21x={} a22x={} a23x={} a24x={} a25x={} a26x={} a27x={} a28x={} a29x={} a2ax={} a2bx={} a2cx={} a2dx={} a2ex={} a2fx={}",
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[0].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[1].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[2].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[3].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[4].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[5].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[6].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[7].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[8].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[9].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[10].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[11].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[12].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[13].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[14].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame[15].get());
				cemuLog_log(LogType::Force,
					"Cemu fast draw context A21 registers: a210={} a211={} a212={} a213={} a214={} a215={} a216={} a217={} a218={} a219={} a21a={} a21b={} a21c={} a21d={} a21e={} a21f={}",
					performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[0].get(), performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[1].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[2].get(), performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[3].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[4].get(), performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[5].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[6].get(), performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[7].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[8].get(), performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[9].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[10].get(), performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[11].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[12].get(), performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[13].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[14].get(), performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame[15].get());
				cemuLog_log(LogType::Force,
					"Cemu fast draw context A22 registers: a220={} a221={} a222={} a223={} a224={} a225={} a226={} a227={} a228={} a229={} a22a={} a22b={} a22c={} a22d={} a22e={} a22f={}",
					performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[0].get(), performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[1].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[2].get(), performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[3].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[4].get(), performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[5].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[6].get(), performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[7].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[8].get(), performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[9].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[10].get(), performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[11].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[12].get(), performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[13].get(),
					performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[14].get(), performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame[15].get());
				cemuLog_log(LogType::Force,
					"Cemu fast draw changed A21 registers: a210={} a211={} a212={} a213={} a214={} a215={} a216={} a217={} a218={} a219={} a21a={} a21b={} a21c={} a21d={} a21e={} a21f={}",
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[0].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[1].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[2].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[3].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[4].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[5].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[6].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[7].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[8].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[9].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[10].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[11].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[12].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[13].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[14].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[15].get());
				cemuLog_log(LogType::Force,
					"Cemu fast draw changed A22 registers: a220={} a221={} a222={} a223={} a224={} a225={} a226={} a227={} a228={} a229={} a22a={} a22b={} a22c={} a22d={} a22e={} a22f={}",
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[16].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[17].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[18].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[19].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[20].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[21].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[22].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[23].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[24].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[25].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[26].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[27].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[28].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[29].get(),
					performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[30].get(), performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame[31].get());
				cemuLog_log(LogType::Force,
					"Cemu Vulkan pipeline activity: sequenceBegins={} queries={} hits={} misses={} readyUses={} unavailableUses={} binds={} redundantBindSkips={} sequenceBeginMs={:.3f} cacheQueryMs={:.3f} bindCallMs={:.3f}",
					performanceMonitor.vk.numVulkanDrawSequenceBeginsPerFrame.get(),
					performanceMonitor.vk.numVulkanPipelineCacheQueriesPerFrame.get(),
					performanceMonitor.vk.numVulkanPipelineCacheHitsPerFrame.get(),
					performanceMonitor.vk.numVulkanPipelineCacheMissesPerFrame.get(),
					performanceMonitor.vk.numVulkanPipelineReadyUsesPerFrame.get(),
					performanceMonitor.vk.numVulkanPipelineUnavailableUsesPerFrame.get(),
					performanceMonitor.vk.numVulkanPipelineBindsPerFrame.get(),
					performanceMonitor.vk.numVulkanPipelineRedundantBindSkipsPerFrame.get(),
					TimerValueToMilliseconds(performanceMonitor.vk.vulkanDrawSequenceBeginTime),
					TimerValueToMilliseconds(performanceMonitor.vk.vulkanPipelineCacheQueryTime),
					TimerValueToMilliseconds(performanceMonitor.vk.vulkanPipelineBindTime));
			}
		}
	}
}

void LattePerformanceMonitor_frameBegin()
{
	performanceMonitor.vk.numDrawBarriersPerFrame.reset();
	performanceMonitor.vk.numInputTextureBarriersPerFrame.reset();
	performanceMonitor.vk.numRenderPassLoadBarriersPerFrame.reset();
	performanceMonitor.vk.numSkippedColorFeedbackBarriersPerFrame.reset();
	performanceMonitor.vk.numBeginRenderpassPerFrame.reset();
	performanceMonitor.vk.numRenderPassFboChangesPerFrame.reset();
	performanceMonitor.vk.numRenderPassSelfDependencySplitsPerFrame.reset();
	performanceMonitor.vk.numRenderPassReopensSameFboPerFrame.reset();
	performanceMonitor.vk.numRenderPassEndsSubmitPerFrame.reset();
	performanceMonitor.vk.numRenderPassEndsPresentationPerFrame.reset();
	performanceMonitor.vk.numRenderPassEndsClearPerFrame.reset();
	performanceMonitor.vk.numRenderPassEndsTextureTransferPerFrame.reset();
	performanceMonitor.vk.numRenderPassEndsBufferTransferPerFrame.reset();
	performanceMonitor.vk.numRenderPassEndsQueryPerFrame.reset();
	performanceMonitor.vk.numRenderPassEndsReadbackPerFrame.reset();
	performanceMonitor.vk.numRenderPassEndsFboTransitionPerFrame.reset();
	performanceMonitor.vk.numRenderPassEndsOtherPerFrame.reset();
	performanceMonitor.vk.numBufferCacheInitialUploadsPerFrame.reset();
	performanceMonitor.vk.numBufferCacheChangedUploadsPerFrame.reset();
	performanceMonitor.vk.numBufferCacheStreamoutUploadsPerFrame.reset();
	performanceMonitor.vk.numBufferCacheUploadBytesPerFrame.reset();
	performanceMonitor.vk.numBufferCachePagesCheckedPerFrame.reset();
	performanceMonitor.vk.numBufferCachePagesChangedPerFrame.reset();
	performanceMonitor.vk.numBufferCacheVertexUploadsPerFrame.reset();
	performanceMonitor.vk.numBufferCacheVertexUploadBytesPerFrame.reset();
	performanceMonitor.vk.numBufferCacheVertexUniformUploadsPerFrame.reset();
	performanceMonitor.vk.numBufferCacheVertexUniformUploadBytesPerFrame.reset();
	performanceMonitor.vk.numBufferCacheGeometryUniformUploadsPerFrame.reset();
	performanceMonitor.vk.numBufferCacheGeometryUniformUploadBytesPerFrame.reset();
	performanceMonitor.vk.numBufferCachePixelUniformUploadsPerFrame.reset();
	performanceMonitor.vk.numBufferCachePixelUniformUploadBytesPerFrame.reset();
	performanceMonitor.vk.numDirectVertexUploadsPerFrame.reset();
	performanceMonitor.vk.numDirectVertexUploadBytesPerFrame.reset();
	performanceMonitor.vk.numDirectVertexProbesPerFrame.reset();
	performanceMonitor.vk.numDirectVertexChangesPerFrame.reset();
	performanceMonitor.vk.numDirectVertexPromotionsPerFrame.reset();
	performanceMonitor.vk.numDirectVertexDemotionsPerFrame.reset();
	performanceMonitor.vk.numDirectVertexSmallRequestsPerFrame.reset();
	performanceMonitor.vk.numDirectVertexHistoryMissesPerFrame.reset();
	performanceMonitor.vk.numDirectVertexCacheHitsPerFrame.reset();
	performanceMonitor.vk.numDirectVertexLearningUploadsPerFrame.reset();
	performanceMonitor.vk.numDirectVertexSameFrameUploadsPerFrame.reset();
	performanceMonitor.vk.numDirectVertexRingRejectsPerFrame.reset();
	performanceMonitor.vk.numDirectVertexOversizedUploadsPerFrame.reset();
	performanceMonitor.vk.numDirectVertexOversizedUploadBytesPerFrame.reset();
	performanceMonitor.vk.numDirectVertexHistoryResetsPerFrame.reset();
	performanceMonitor.vk.numDirectVertexPromotionsLe256PerFrame.reset();
	performanceMonitor.vk.numDirectVertexPromotionsLe512PerFrame.reset();
	performanceMonitor.vk.numDirectVertexPromotionsLe1024PerFrame.reset();
	performanceMonitor.vk.numDirectVertexPromotionsLe2048PerFrame.reset();
	performanceMonitor.vk.numDirectVertexPromotionsLe4096PerFrame.reset();
	performanceMonitor.vk.numDirectVertexBindsLe256PerFrame.reset();
	performanceMonitor.vk.numDirectVertexBindsLe512PerFrame.reset();
	performanceMonitor.vk.numDirectVertexBindsLe1024PerFrame.reset();
	performanceMonitor.vk.numDirectVertexBindsLe2048PerFrame.reset();
	performanceMonitor.vk.numDirectVertexBindsLe4096PerFrame.reset();
	performanceMonitor.vk.numFastDrawPassEndsStreamoutPerFrame.reset();
	performanceMonitor.vk.numFastDrawPassEndsQueueEmptyPerFrame.reset();
	performanceMonitor.vk.numFastDrawPassEndsTextureChangePerFrame.reset();
	performanceMonitor.vk.numFastDrawPassEndsContextChangePerFrame.reset();
	for (auto& counter : performanceMonitor.vk.numFastDrawPassEndsContextBucketPerFrame)
		counter.reset();
	for (auto& counter : performanceMonitor.vk.numFastDrawPassEndsContextA2BucketPerFrame)
		counter.reset();
	for (auto& counter : performanceMonitor.vk.numFastDrawPassEndsContextA21RegisterPerFrame)
		counter.reset();
	for (auto& counter : performanceMonitor.vk.numFastDrawPassEndsContextA22RegisterPerFrame)
		counter.reset();
	for (auto& counter : performanceMonitor.vk.numFastDrawContextRegisterChangesPerFrame)
		counter.reset();
	performanceMonitor.vk.numVulkanDrawSequenceBeginsPerFrame.reset();
	performanceMonitor.vk.numVulkanPipelineCacheQueriesPerFrame.reset();
	performanceMonitor.vk.numVulkanPipelineCacheHitsPerFrame.reset();
	performanceMonitor.vk.numVulkanPipelineCacheMissesPerFrame.reset();
	performanceMonitor.vk.numVulkanPipelineReadyUsesPerFrame.reset();
	performanceMonitor.vk.numVulkanPipelineUnavailableUsesPerFrame.reset();
	performanceMonitor.vk.numVulkanPipelineBindsPerFrame.reset();
	performanceMonitor.vk.numVulkanPipelineRedundantBindSkipsPerFrame.reset();
	performanceMonitor.vk.numFastDrawPassEndsSamplerChangePerFrame.reset();
	performanceMonitor.vk.numFastDrawPassEndsUnsupportedType3PerFrame.reset();
	performanceMonitor.vk.numFastDrawPassEndsUnsupportedPacketPerFrame.reset();
}
