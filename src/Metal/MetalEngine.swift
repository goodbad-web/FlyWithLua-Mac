import Metal
import Foundation

/// Core Metal engine for FlyWithLua-Mac.
public final class MetalEngine {
    public static let shared = MetalEngine()
    
    public let device: MTLDevice
    public let commandQueue: MTLCommandQueue
    
    private init() {
        guard let device = MTLCreateSystemDefaultDevice() else {
            fatalError("FlyWithLua-Mac: Could not create Metal device.")
        }
        self.device = device
        
        guard let queue = device.makeCommandQueue() else {
            fatalError("FlyWithLua-Mac: Could not create Metal command queue.")
        }
        self.commandQueue = queue
        
        print("FlyWithLua-Mac: Metal engine initialized on \(device.name)")
    }
    
    /// Creates an offscreen texture for rendering.
    public func makeOffscreenTexture(width: Int, height: Int) -> MTLTexture? {
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: width,
            height: height,
            mipmapped: false
        )
        descriptor.usage = [.renderTarget, .shaderRead]
        descriptor.storageMode = .private
        
        return device.makeTexture(descriptor: descriptor)
    }

    /// Copies a texture on the GPU when the source and destination formats match.
    public func copyTexture(_ source: MTLTexture, to destination: MTLTexture) -> Bool {
        guard source.pixelFormat == destination.pixelFormat else {
            return false
        }
        guard source.width == destination.width, source.height == destination.height else {
            return false
        }
        guard let commandBuffer = commandQueue.makeCommandBuffer(),
              let blitEncoder = commandBuffer.makeBlitCommandEncoder() else {
            return false
        }

        let copySize = MTLSize(width: source.width, height: source.height, depth: 1)
        blitEncoder.copy(
            from: source,
            sourceSlice: 0,
            sourceLevel: 0,
            sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
            sourceSize: copySize,
            to: destination,
            destinationSlice: 0,
            destinationLevel: 0,
            destinationOrigin: MTLOrigin(x: 0, y: 0, z: 0)
        )
        blitEncoder.endEncoding()
        commandBuffer.commit()
        commandBuffer.waitUntilCompleted()

        return commandBuffer.status == .completed
    }
}
