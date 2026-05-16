import Metal
import Foundation

/// Bridges Metal textures to X-Plane's texture system.
public final class MetalTextureBridge {
    private let engine: MetalEngine
    public private(set) var xplaneTextureID: Int32 = 0
    
    public init(engine: MetalEngine = .shared) {
        self.engine = engine
    }
    
    /// Initializes an X-Plane texture ID for the given dimensions.
    public func setupXPlaneTexture() {
        var textureID: Int32 = 0
        XPLMGenerateTextureNumbers(&textureID, 1)
        self.xplaneTextureID = textureID
    }
    
    /// Synchronizes Metal texture data to X-Plane.
    /// This is a simplified version using CPU-side copy.
    public func syncToXPlane(texture: MTLTexture) {
        let width = texture.width
        let height = texture.height
        let region = MTLRegionMake2D(0, 0, width, height)
        let bytesPerRow = width * 4
        
        var data = [UInt8](repeating: 0, count: bytesPerRow * height)
        texture.getBytes(&data, bytesPerRow: bytesPerRow, from: region, mipmapLevel: 0)
        
        XPLMBindTexture2d(xplaneTextureID, 0)
        
        // Upload data to OpenGL texture
        glTexSubImage2D(GLenum(GL_TEXTURE_2D), 0, 0, 0, Int32(width), Int32(height), GLenum(GL_RGBA), GLenum(GL_UNSIGNED_BYTE), &data)
    }
}
