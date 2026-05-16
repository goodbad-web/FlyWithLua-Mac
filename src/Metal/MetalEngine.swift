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
            pixelFormat: .rgba8Unorm,
            width: width,
            height: height,
            mipmapped: false
        )
        descriptor.usage = [.renderTarget, .shaderRead]
        descriptor.storageMode = .shared
        
        return device.makeTexture(descriptor: descriptor)
    }
}
