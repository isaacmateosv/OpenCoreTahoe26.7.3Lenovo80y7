import AVFoundation; import VideoToolbox
// Encode 60 frames to H.264 / HEVC, then decode them with HARDWARE REQUIRED.
func test(_ codec: AVVideoCodecType, _ name: String) {
  let url = URL(fileURLWithPath: NSTemporaryDirectory() + "vt-\(name).mov"); try? FileManager.default.removeItem(at: url)
  let w = try! AVAssetWriter(outputURL: url, fileType: .mov)
  let inp = AVAssetWriterInput(mediaType: .video, outputSettings: [AVVideoCodecKey: codec, AVVideoWidthKey: 1280, AVVideoHeightKey: 720])
  let ad = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: inp, sourcePixelBufferAttributes: [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA, kCVPixelBufferWidthKey as String: 1280, kCVPixelBufferHeightKey as String: 720])
  w.add(inp); w.startWriting(); w.startSession(atSourceTime: .zero)
  for i in 0..<60 { while !inp.isReadyForMoreMediaData { usleep(1000) }
    var pb: CVPixelBuffer?; CVPixelBufferPoolCreatePixelBuffer(nil, ad.pixelBufferPool!, &pb)
    CVPixelBufferLockBaseAddress(pb!, []); memset(CVPixelBufferGetBaseAddress(pb!), Int32(i*4 % 255), CVPixelBufferGetDataSize(pb!)); CVPixelBufferUnlockBaseAddress(pb!, [])
    ad.append(pb!, withPresentationTime: CMTime(value: Int64(i), timescale: 30)) }
  inp.markAsFinished(); let g = DispatchSemaphore(value: 0); w.finishWriting { g.signal() }; g.wait()
  let asset = AVURLAsset(url: url); let track = asset.tracks(withMediaType: .video)[0]
  let r = try! AVAssetReader(asset: asset); let out = AVAssetReaderTrackOutput(track: track, outputSettings: nil); r.add(out); r.startReading()
  let fd = track.formatDescriptions[0] as! CMVideoFormatDescription
  var sess: VTDecompressionSession?
  let spec = [kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: true] as CFDictionary
  let st = VTDecompressionSessionCreate(allocator: nil, formatDescription: fd, decoderSpecification: spec, imageBufferAttributes: nil, outputCallback: nil, decompressionSessionOut: &sess)
  guard st == noErr, let s = sess else { print(name, "HW decoder session FAILED", st); return }
  var ok = 0, bad = 0
  while let sb = out.copyNextSampleBuffer() { if CMSampleBufferGetNumSamples(sb) == 0 { continue }
    let e = VTDecompressionSessionDecodeFrame(s, sampleBuffer: sb, flags: [], infoFlagsOut: nil) { status, _, img, _, _ in if status == noErr && img != nil { ok += 1 } else { bad += 1 } }
    if e != noErr { bad += 1 } }
  VTDecompressionSessionWaitForAsynchronousFrames(s)
  print(name, "hardware decode: \(ok) frames OK, \(bad) failed")
}
test(.h264, "H.264"); test(.hevc, "HEVC")
