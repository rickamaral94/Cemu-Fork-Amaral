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
					"Cemu performance drop: fps={:.2f} drawCallsPerFrame={} fastDrawCallsPerFrame={} renderCpuMs={:.3f} recompilerLeavesPerSecond={} threadLeavesPerSecond={} indexUploadKiBPerFrame={} vkPipelines={} vkDescriptorSets={} vkImages={} vkImageViews={} vkRenderPasses={} vkFramebuffers={} barriersLastFrame={} inputTextureBarriersLastFrame={} renderPassLoadBarriersLastFrame={} skippedColorFeedbackBarriersLastFrame={} beginRenderPassesLastFrame={} renderPassFboChangesLastFrame={} renderPassSelfDependencySplitsLastFrame={} renderPassReopensSameFboLastFrame={} bufferCache=[initialUploads:{},changedUploads:{},streamoutUploads:{},uploadKiB:{},pagesChecked:{},pagesChanged:{},bridgedPages:{},mergedRuns:{}] renderPassEndsLastFrame=[submit:{},present:{},clear:{},textureTransfer:{},bufferTransfer:{},query:{},readback:{},fboTransition:{},other:{}]",
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
					performanceMonitor.vk.numBufferCacheBridgedPagesPerFrame.get(),
					performanceMonitor.vk.numBufferCacheMergedRunsPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsSubmitPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsPresentationPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsClearPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsTextureTransferPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsBufferTransferPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsQueryPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsReadbackPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsFboTransitionPerFrame.get(),
					performanceMonitor.vk.numRenderPassEndsOtherPerFrame.get());
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
	performanceMonitor.vk.numBufferCacheBridgedPagesPerFrame.reset();
	performanceMonitor.vk.numBufferCacheMergedRunsPerFrame.reset();
}
