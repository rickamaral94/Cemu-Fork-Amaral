#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "WindowSystem.h"
#include "Cemu/Logging/CemuLogging.h"
#include "config/CemuConfig.h"

performanceMonitor_t performanceMonitor{};

namespace
{
constexpr uint32 kTelemetryLogIntervalMs = 2000;
uint32 s_lastTelemetryLog = 0;
std::vector<double> s_frameTimeSamples;
std::vector<double> s_renderCpuSamples;
uint64 s_diagnosticOverheadCycles = 0;
uint64 s_previousFrameEnd = 0;
uint32 s_previousPipelineCount = 0;
struct DiagnosticWindow
{
	uint32 frames{};
	uint64 counters[9]{};
	double times[10]{};
};
DiagnosticWindow s_diagnosticWindow;

double Percentile(std::vector<double> values, double percentile)
{
	if (values.empty())
		return 0.0;
	std::sort(values.begin(), values.end());
	const size_t index = std::min<size_t>(static_cast<size_t>(std::ceil(percentile * values.size())) - 1, values.size() - 1);
	return values[index];
}

double TimerValueToMilliseconds(LattePerfStatTimer& timer)
{
	return static_cast<double>(PPCTimer_tscToMicroseconds(timer.getPreviousFrameValue())) / 1000.0;
}

}

void LattePerformanceMonitor_frameEnd()
{
	const uint64 diagnosticStart = PPCTimer_getRawTsc();
	// per-frame stats
	performanceMonitor.gpuTime_shaderCreate.frameFinished();
	performanceMonitor.gpuTime_frameTime.frameFinished();
	performanceMonitor.gpuTime_idleTime.frameFinished();
	performanceMonitor.gpuTime_commandBuffer.frameFinishedIncludingActive();
	performanceMonitor.gpuTime_continuousDrawPass.frameFinishedIncludingActive();
	for (auto& transferTime : performanceMonitor.commandProcessor.genericTransferTime)
		transferTime.frameFinished();
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
	performanceMonitor.vk.vulkanFirstDrawTime.frameFinished();
	performanceMonitor.vk.vulkanContinuedDrawTime.frameFinished();
	performanceMonitor.vk.queueSubmitTime.frameFinished();
	performanceMonitor.vk.acquireImageTime.frameFinished();
	performanceMonitor.vk.queuePresentTime.frameFinished();
	performanceMonitor.vk.presentWaitTime.frameFinished();
	performanceMonitor.vk.commandBufferFenceWaitTime.frameFinished();
	// Accumulate complete frames before per-frame counters are reset. Historical
	// telemetry below keeps its original last-frame and rolling-FPS semantics.
	++s_diagnosticWindow.frames;
	LattePerfStatTimer* windowTimers[] = {
		&performanceMonitor.gpuTime_frameTime, &performanceMonitor.gpuTime_idleTime,
		&performanceMonitor.gpuTime_fenceTime, &performanceMonitor.vk.commandBufferFenceWaitTime,
		&performanceMonitor.gpuTime_waitForAsync, &performanceMonitor.gpuTime_shaderCreate,
		&performanceMonitor.vk.queueSubmitTime, &performanceMonitor.vk.acquireImageTime,
		&performanceMonitor.vk.queuePresentTime, &performanceMonitor.vk.presentWaitTime};
	for (size_t i = 0; i < std::size(windowTimers); ++i)
		s_diagnosticWindow.times[i] += TimerValueToMilliseconds(*windowTimers[i]);
	LattePerfStatCounter* windowCounters[] = {
		&performanceMonitor.vk.numQueueSubmitsPerFrame, &performanceMonitor.vk.numSubmittedCommandBuffersPerFrame,
		&performanceMonitor.vk.numAcquireCallsPerFrame, &performanceMonitor.vk.numPresentCallsPerFrame,
		&performanceMonitor.vk.numPresentWaitsPerFrame, &performanceMonitor.vk.numSwapchainRecreatesPerFrame,
		&performanceMonitor.vk.numDrawBarriersPerFrame, &performanceMonitor.vk.numBeginRenderpassPerFrame,
		&performanceMonitor.vk.numVulkanPipelineBindsPerFrame};
	for (size_t i = 0; i < std::size(windowCounters); ++i)
	{
		s_diagnosticWindow.counters[i] += windowCounters[i]->get();
		// Presentation happens after frameEnd. Retain these new counters until
		// the next frameEnd, just like the Vulkan duration timers.
		if (i < 6)
			windowCounters[i]->reset();
	}

	if (GetConfig().overlay.debug)
	{
		s_renderCpuSamples.emplace_back(TimerValueToMilliseconds(performanceMonitor.gpuTime_frameTime));
		const uint64 frameEnd = PPCTimer_getRawTsc();
		if (s_previousFrameEnd != 0)
			s_frameTimeSamples.emplace_back(static_cast<double>(PPCTimer_tscToMicroseconds(frameEnd - s_previousFrameEnd)) / 1000.0);
		s_previousFrameEnd = frameEnd;
	}
	else
		s_previousFrameEnd = 0;

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
			s_diagnosticWindow = {};
			s_frameTimeSamples.clear();
			s_renderCpuSamples.clear();
			s_previousFrameEnd = 0;
			LatteOverlay_updateStats(0.0, 0, 0);
			WindowSystem::UpdateWindowTitles(false, false, 0.0);
		}
		else
		{
			const double renderFrameMs = TimerValueToMilliseconds(performanceMonitor.gpuTime_frameTime);
			const double commandIdleMs = TimerValueToMilliseconds(performanceMonitor.gpuTime_idleTime);
			const double nonIdleMs = std::max(renderFrameMs - commandIdleMs, 0.0);
			const double commandBufferMs = TimerValueToMilliseconds(performanceMonitor.gpuTime_commandBuffer);
			const double continuousPassMs = TimerValueToMilliseconds(performanceMonitor.gpuTime_continuousDrawPass);
			const double outsideCommandBufferMs = std::max(nonIdleMs - commandBufferMs, 0.0);
			const double genericPathMs = std::max(commandBufferMs - continuousPassMs, 0.0);
			const uint32 drawCallsPerFrame = drawCallCounter / elapsedFrames;
			const uint32 fastDrawCallsPerFrame = fastDrawCallCounter / elapsedFrames;
			const uint32 pipelineCount = performanceMonitor.vk.numGraphicPipelines.get();
			const uint32 pipelineCreations = pipelineCount >= s_previousPipelineCount ? pipelineCount - s_previousPipelineCount : 0;
			s_previousPipelineCount = pipelineCount;
			LatteOverlay_updateStats(fps, drawCallsPerFrame, fastDrawCallsPerFrame);
			WindowSystem::UpdateWindowTitles(false, false, fps);
			const uint32 now = GetTickCount();
			if (now - s_lastTelemetryLog >= kTelemetryLogIntervalMs)
			{
				s_lastTelemetryLog = now;
				cemuLog_log(LogType::Force,
					"Cemu performance telemetry: fps={:.2f} drawCallsPerFrame={} fastDrawCallsPerFrame={} renderCpuMs={:.3f} commandIdleMs={:.3f} nonIdleMs={:.3f} fenceWaitMs={:.3f} asyncWaitMs={:.3f} shaderCreateMs={:.3f}",
					fps, drawCallsPerFrame, fastDrawCallsPerFrame,
					renderFrameMs, commandIdleMs, nonIdleMs,
					TimerValueToMilliseconds(performanceMonitor.gpuTime_fenceTime),
					TimerValueToMilliseconds(performanceMonitor.gpuTime_waitForAsync),
					TimerValueToMilliseconds(performanceMonitor.gpuTime_shaderCreate));
			}
			cemuLog_log(LogType::Force,
				"Cemu Vulkan window v1: durationMs={} fpsEffective={:.2f} frames={} drawCallsPerFrame={} renderCpuMs={:.3f} commandIdleMs={:.3f} nonIdleMs={:.3f} fenceWaitMs={:.3f} commandBufferFenceWaitMs={:.3f} asyncWaitMs={:.3f} shaderCreateMs={:.3f} pipelines={} pipelineCreations={} pipelineChanges={} queueSubmitCalls={} commandBuffers={} queueSubmitCpuMs={:.3f} acquireCalls={} acquireCpuMs={:.3f} presentCalls={} presentCallCpuMs={:.3f} presentWaitCalls={} presentWaitMs={:.3f} barriers={} layoutTransitions=unavailable beginRenderPasses={} swapchainRecreates={} gpuTimeMs=unavailable gpuReason=timestamp-instrumentation-not-enabled coveragePct=0 droppedSamples=0",
				elapsedTime, s_diagnosticWindow.frames * 1000.0 / elapsedTime, s_diagnosticWindow.frames, drawCallsPerFrame,
				s_diagnosticWindow.times[0] / s_diagnosticWindow.frames,
				s_diagnosticWindow.times[1] / s_diagnosticWindow.frames,
				std::max(s_diagnosticWindow.times[0] - s_diagnosticWindow.times[1], 0.0) / s_diagnosticWindow.frames,
				s_diagnosticWindow.times[2] / s_diagnosticWindow.frames,
				s_diagnosticWindow.times[3] / s_diagnosticWindow.frames,
				s_diagnosticWindow.times[4] / s_diagnosticWindow.frames,
				s_diagnosticWindow.times[5] / s_diagnosticWindow.frames,
				pipelineCount, pipelineCreations, s_diagnosticWindow.counters[8],
				s_diagnosticWindow.counters[0], s_diagnosticWindow.counters[1], s_diagnosticWindow.times[6],
				s_diagnosticWindow.counters[2], s_diagnosticWindow.times[7],
				s_diagnosticWindow.counters[3], s_diagnosticWindow.times[8],
				s_diagnosticWindow.counters[4], s_diagnosticWindow.times[9],
				s_diagnosticWindow.counters[6], s_diagnosticWindow.counters[7], s_diagnosticWindow.counters[5]);
			s_diagnosticWindow = {};
			if (GetConfig().overlay.debug && !s_frameTimeSamples.empty())
			{
				const auto countAbove = [](const std::vector<double>& values, double threshold) {
					return std::count_if(values.begin(), values.end(), [threshold](double value) { return value > threshold; });
				};
				cemuLog_log(LogType::Force,
					"Cemu Vulkan detailed window v1: frameMs=[median:{:.3f},p95:{:.3f},p99:{:.3f},max:{:.3f}] cpuMs=[median:{:.3f},p95:{:.3f},p99:{:.3f},max:{:.3f}] over16_7={} over33_3={} over50={} samples={} overheadMs={:.3f}",
					Percentile(s_frameTimeSamples, 0.50), Percentile(s_frameTimeSamples, 0.95),
					Percentile(s_frameTimeSamples, 0.99), *std::max_element(s_frameTimeSamples.begin(), s_frameTimeSamples.end()),
					Percentile(s_renderCpuSamples, 0.50), Percentile(s_renderCpuSamples, 0.95),
					Percentile(s_renderCpuSamples, 0.99), *std::max_element(s_renderCpuSamples.begin(), s_renderCpuSamples.end()),
					countAbove(s_frameTimeSamples, 16.7), countAbove(s_frameTimeSamples, 33.3),
					countAbove(s_frameTimeSamples, 50.0), s_frameTimeSamples.size(),
					static_cast<double>(PPCTimer_tscToMicroseconds(s_diagnosticOverheadCycles)) / 1000.0);
			}
			s_frameTimeSamples.clear();
			s_renderCpuSamples.clear();
			s_diagnosticOverheadCycles = 0;
			if (fps < 28.0)
			{
				cemuLog_log(LogType::Force,
					"Cemu performance drop: fps={:.2f} drawCallsPerFrame={} fastDrawCallsPerFrame={} renderCpuMs={:.3f} commandIdleMs={:.3f} nonIdleMs={:.3f} recompilerLeavesPerSecond={} threadLeavesPerSecond={} indexUploadKiBPerFrame={} vkPipelines={} vkDescriptorSets={} vkImages={} vkImageViews={} vkRenderPasses={} vkFramebuffers={} barriersLastFrame={} inputTextureBarriersLastFrame={} renderPassLoadBarriersLastFrame={} skippedColorFeedbackBarriersLastFrame={} beginRenderPassesLastFrame={} renderPassFboChangesLastFrame={} renderPassSelfDependencySplitsLastFrame={} renderPassReopensSameFboLastFrame={} bufferCache=[initialUploads:{},changedUploads:{},streamoutUploads:{},uploadKiB:{},pagesChecked:{},pagesChanged:{}] uploadSources=[vertex:{}/{},vsUniform:{}/{},gsUniform:{}/{},psUniform:{}/{},directVertex:{}/{}] directVertexClassifier=[probes:{},changes:{},promotions:{},demotions:{}] directVertexEligibility=[smallRequests:{},historyMisses:{},cacheHits:{},learningUploads:{},sameFrameUploads:{},ringRejects:{},oversizedUploads:{},oversizedKiB:{},historyResets:{}] directVertexPromotionSizes=[le256:{},le512:{},le1024:{},le2048:{},le4096:{}] directVertexBindSizes=[le256:{},le512:{},le1024:{},le2048:{},le4096:{}] fastDrawPassEnds=[streamout:{},queueEmpty:{},textureChange:{},contextChange:{},samplerChange:{},unsupportedType3:{},unsupportedPacket:{}] renderPassEndsLastFrame=[submit:{},present:{},clear:{},textureTransfer:{},bufferTransfer:{},query:{},readback:{},fboTransition:{},other:{}]",
					fps, drawCallsPerFrame, fastDrawCallsPerFrame,
					renderFrameMs, commandIdleMs, nonIdleMs, rlps, tlps,
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
					"Cemu Vulkan pipeline activity: sequenceBegins={} queries={} hits={} misses={} readyUses={} unavailableUses={} binds={} redundantBindSkips={} sequenceBeginMs={:.3f} cacheQueryMs={:.3f} bindCallMs={:.3f} firstDrawMs={:.3f} continuedDrawMs={:.3f}",
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
					TimerValueToMilliseconds(performanceMonitor.vk.vulkanPipelineBindTime),
					TimerValueToMilliseconds(performanceMonitor.vk.vulkanFirstDrawTime),
					TimerValueToMilliseconds(performanceMonitor.vk.vulkanContinuedDrawTime));
				cemuLog_log(LogType::Force,
					"Cemu command processor timing: nonIdleMs={:.3f} commandBufferMs={:.3f} continuousPassMs={:.3f} genericPathMs={:.3f} outsideCommandBufferMs={:.3f}",
					nonIdleMs, commandBufferMs, continuousPassMs, genericPathMs, outsideCommandBufferMs);
				cemuLog_log(LogType::Force,
					"Cemu command processor packets: continuous=[resource:{}/{},aluConst:{}/{},context:{}/{},draw:{}/{},other:{}/{}] generic=[registers:{}/{},waitSync:{}/{},transfer:{}/{},draw:{}/{},other:{}/{}]",
					performanceMonitor.commandProcessor.continuousPackets[0], performanceMonitor.commandProcessor.continuousWords[0],
					performanceMonitor.commandProcessor.continuousPackets[1], performanceMonitor.commandProcessor.continuousWords[1],
					performanceMonitor.commandProcessor.continuousPackets[2], performanceMonitor.commandProcessor.continuousWords[2],
					performanceMonitor.commandProcessor.continuousPackets[3], performanceMonitor.commandProcessor.continuousWords[3],
					performanceMonitor.commandProcessor.continuousPackets[4], performanceMonitor.commandProcessor.continuousWords[4],
					performanceMonitor.commandProcessor.genericPackets[0], performanceMonitor.commandProcessor.genericWords[0],
					performanceMonitor.commandProcessor.genericPackets[1], performanceMonitor.commandProcessor.genericWords[1],
					performanceMonitor.commandProcessor.genericPackets[2], performanceMonitor.commandProcessor.genericWords[2],
					performanceMonitor.commandProcessor.genericPackets[3], performanceMonitor.commandProcessor.genericWords[3],
					performanceMonitor.commandProcessor.genericPackets[4], performanceMonitor.commandProcessor.genericWords[4]);
				cemuLog_log(LogType::Force,
					"Cemu generic register packets: context={}/{} resource={}/{} aluConst={}/{} sampler={}/{} config={}/{} ctlLoop={}/{}",
					performanceMonitor.commandProcessor.genericRegisterPackets[0], performanceMonitor.commandProcessor.genericRegisterWords[0],
					performanceMonitor.commandProcessor.genericRegisterPackets[1], performanceMonitor.commandProcessor.genericRegisterWords[1],
					performanceMonitor.commandProcessor.genericRegisterPackets[2], performanceMonitor.commandProcessor.genericRegisterWords[2],
					performanceMonitor.commandProcessor.genericRegisterPackets[3], performanceMonitor.commandProcessor.genericRegisterWords[3],
					performanceMonitor.commandProcessor.genericRegisterPackets[4], performanceMonitor.commandProcessor.genericRegisterWords[4],
					performanceMonitor.commandProcessor.genericRegisterPackets[5], performanceMonitor.commandProcessor.genericRegisterWords[5]);
				cemuLog_log(LogType::Force,
					"Cemu generic register shape: loads=[context:{},resource:{},aluConst:{},sampler:{},config:{},ctlLoop:{}] contextWidth=[1:{},2to4:{},5to8:{},9plus:{}] resourceWidth=[1:{},2to4:{},5to8:{},9plus:{}]",
					performanceMonitor.commandProcessor.genericRegisterLoadPackets[0], performanceMonitor.commandProcessor.genericRegisterLoadPackets[1],
					performanceMonitor.commandProcessor.genericRegisterLoadPackets[2], performanceMonitor.commandProcessor.genericRegisterLoadPackets[3],
					performanceMonitor.commandProcessor.genericRegisterLoadPackets[4], performanceMonitor.commandProcessor.genericRegisterLoadPackets[5],
					performanceMonitor.commandProcessor.genericRegisterWidthPackets[0][0], performanceMonitor.commandProcessor.genericRegisterWidthPackets[0][1],
					performanceMonitor.commandProcessor.genericRegisterWidthPackets[0][2], performanceMonitor.commandProcessor.genericRegisterWidthPackets[0][3],
					performanceMonitor.commandProcessor.genericRegisterWidthPackets[1][0], performanceMonitor.commandProcessor.genericRegisterWidthPackets[1][1],
					performanceMonitor.commandProcessor.genericRegisterWidthPackets[1][2], performanceMonitor.commandProcessor.genericRegisterWidthPackets[1][3]);
				cemuLog_log(LogType::Force,
					"Cemu generic transfer timing: scanbuffer={}/{:.3f} clear={}/{:.3f} surfaceCopy={}/{:.3f}",
					performanceMonitor.commandProcessor.genericTransferPackets[0],
					TimerValueToMilliseconds(performanceMonitor.commandProcessor.genericTransferTime[0]),
					performanceMonitor.commandProcessor.genericTransferPackets[1],
					TimerValueToMilliseconds(performanceMonitor.commandProcessor.genericTransferTime[1]),
					performanceMonitor.commandProcessor.genericTransferPackets[2],
					TimerValueToMilliseconds(performanceMonitor.commandProcessor.genericTransferTime[2]));
			}
		}
	}
	s_diagnosticOverheadCycles += PPCTimer_getRawTsc() - diagnosticStart;
}

void LattePerformanceMonitor_frameBegin()
{
	performanceMonitor.commandProcessor.continuousPackets.fill(0);
	performanceMonitor.commandProcessor.continuousWords.fill(0);
	performanceMonitor.commandProcessor.genericPackets.fill(0);
	performanceMonitor.commandProcessor.genericWords.fill(0);
	performanceMonitor.commandProcessor.genericRegisterPackets.fill(0);
	performanceMonitor.commandProcessor.genericRegisterWords.fill(0);
	performanceMonitor.commandProcessor.genericRegisterLoadPackets.fill(0);
	performanceMonitor.commandProcessor.genericTransferPackets.fill(0);
	for (auto& widthPackets : performanceMonitor.commandProcessor.genericRegisterWidthPackets)
		widthPackets.fill(0);
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
