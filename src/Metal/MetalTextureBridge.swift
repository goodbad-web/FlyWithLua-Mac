import Metal
import Foundation

/// Bridges Metal textures to X-Plane's texture system.
public final class MetalTextureBridge {
    private let engine: MetalEngine
    public private(set) var xplaneTextureID: Int32 = 0

    private let transferBacking: TransferBacking

    public init(engine: MetalEngine = .shared) {
        self.engine = engine
        self.transferBacking = TransferBacking(engine: engine)
    }

    /// Initializes an X-Plane texture ID for the given dimensions.
    public func setupXPlaneTexture() {
        guard xplaneTextureID == 0 else {
            return
        }

        var textureID: Int32 = 0
        XPLMGenerateTextureNumbers(&textureID, 1)
        self.xplaneTextureID = textureID
    }

    /// Synchronizes Metal texture data to X-Plane.
    public func syncToXPlane(texture: MTLTexture) {
        if xplaneTextureID == 0 {
            setupXPlaneTexture()
        }

        guard xplaneTextureID != 0 else {
            return
        }

        transferBacking.sync(
            source: texture,
            xplaneTextureID: xplaneTextureID
        )
    }
}

private final class TransferBacking {
    private struct UploadFormat {
        let glFormat: GLenum
        let glType: GLenum
    }

    private var width: Int = 0
    private var height: Int = 0
    private var stagingBuffer = Data()

    init(engine _: MetalEngine) {
    }

    func sync(source: MTLTexture, xplaneTextureID: Int32) {
        let sourceWidth = source.width
        let sourceHeight = source.height

        if sourceWidth <= 0 || sourceHeight <= 0 {
            return
        }

        if width != sourceWidth || height != sourceHeight {
            rebuildBacking(width: sourceWidth, height: sourceHeight)
        }

        _ = uploadUsingStagingBuffer(
            source: source,
            width: sourceWidth,
            height: sourceHeight,
            xplaneTextureID: xplaneTextureID
        )
    }

    private func rebuildBacking(width: Int, height: Int) {
        self.width = width
        self.height = height
        let requiredBytes = width * height * 4
        if stagingBuffer.count != requiredBytes {
            stagingBuffer = Data(count: requiredBytes)
        }
    }

    private func uploadUsingStagingBuffer(source: MTLTexture, width: Int, height: Int, xplaneTextureID: Int32) -> Bool {
        guard let uploadFormat = uploadFormat(for: source.pixelFormat) else {
            return false
        }

        let bytesPerRow = width * 4
        let requiredBytes = bytesPerRow * height
        if stagingBuffer.count != requiredBytes {
            stagingBuffer = Data(count: requiredBytes)
        }

        let region = MTLRegionMake2D(0, 0, width, height)
        let copied = stagingBuffer.withUnsafeMutableBytes { rawBuffer -> Bool in
            guard let baseAddress = rawBuffer.baseAddress else {
                return false
            }

            source.getBytes(baseAddress, bytesPerRow: bytesPerRow, from: region, mipmapLevel: 0)

            XPLMBindTexture2d(xplaneTextureID, 0)
            glPixelStorei(GLenum(GL_UNPACK_ALIGNMENT), 4)
            glPixelStorei(GLenum(GL_UNPACK_ROW_LENGTH), 0)
            glTexSubImage2D(
                GLenum(GL_TEXTURE_2D),
                0,
                0,
                0,
                Int32(width),
                Int32(height),
                uploadFormat.glFormat,
                uploadFormat.glType,
                baseAddress
            )

            return true
        }

        return copied
    }

    private func uploadFormat(for pixelFormat: MTLPixelFormat) -> UploadFormat? {
        switch pixelFormat {
        case .bgra8Unorm, .bgra8Unorm_srgb:
            return UploadFormat(
                glFormat: GLenum(GL_BGRA),
                glType: GLenum(GL_UNSIGNED_INT_8_8_8_8_REV)
            )
        case .rgba8Unorm, .rgba8Unorm_srgb:
            return UploadFormat(
                glFormat: GLenum(GL_RGBA),
                glType: GLenum(GL_UNSIGNED_BYTE)
            )
        default:
            return nil
        }
    }
}
