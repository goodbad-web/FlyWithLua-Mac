import CoreGraphics
import CoreText
import Foundation

/// Shared high-DPI text renderer used by in-sim overlays.
///
/// X-Plane exposes drawing coordinates in boxels.  This renderer keeps those
/// coordinates unchanged and rasterizes the glyphs at a higher backing
/// resolution before drawing them as alpha textures.  The renderer is only
/// touched from the X-Plane drawing callback, so its OpenGL cache remains on
/// the thread that owns the current graphics context.
public final class HUDTextRenderer {
    public static let shared = HUDTextRenderer()

    private struct AtlasKey: Hashable {
        let family: String
        let weight: Int
        let sizeQuarterPoints: Int
        let rasterScale: Int
    }

    private struct GlyphKey: Hashable {
        let scalar: UInt32
    }

    private struct GlyphRecord {
        let textureX: Int
        let textureY: Int
        let textureWidth: Int
        let textureHeight: Int
        let originX: CGFloat
        let originY: CGFloat
        let advance: CGFloat
    }

    private final class Atlas {
        static let width = 1024
        static let height = 1024
        static let padding = 2

        let key: AtlasKey
        var textureID: Int32 = 0
        var glyphs: [GlyphKey: GlyphRecord] = [:]
        var cursorX = 0
        var cursorY = 0
        var rowHeight = 0
        var lastUsed: UInt64 = 0

        init(key: AtlasKey) {
            self.key = key
        }

    }

    private struct PreparedGlyph {
        let record: GlyphRecord
        let atlas: Atlas
    }

    private struct TextureState {
        let activeTexture: GLenum
        let textureID: Int32
    }

    private struct TextureMatrixState {
        let previousMatrixMode: GLenum
    }

    private let rasterScale: CGFloat = 2.0
    private let maximumLogicalFontSize: CGFloat = 256.0
    private let maximumAtlases = 8
    private var atlases: [AtlasKey: Atlas] = [:]
    private var useCounter: UInt64 = 0

    private init() {}

    /// Measures a string in X-Plane boxels.
    public func measure(text: String,
                        logicalSize: CGFloat,
                        family: String,
                        weight: Int) -> CGFloat? {
        guard !text.isEmpty,
              logicalSize.isFinite,
              logicalSize > 0,
              logicalSize <= maximumLogicalFontSize else {
            return text.isEmpty ? 0 : nil
        }

        let font = makeFont(family: family, logicalSize: logicalSize, weight: weight)
        let attributed = NSAttributedString(string: text, attributes: [
            NSAttributedString.Key(kCTFontAttributeName as String): font
        ])
        let line = CTLineCreateWithAttributedString(attributed as CFAttributedString)
        let width = CTLineGetTypographicBounds(line, nil, nil, nil)
        guard width.isFinite else { return nil }
        return CGFloat(width) / rasterScale
    }

    /// Draws a string using the current OpenGL color.
    public func draw(text: String,
                     x: CGFloat,
                     y: CGFloat,
                     logicalSize: CGFloat,
                     family: String,
                     weight: Int) -> Bool {
        guard !text.isEmpty,
              x.isFinite,
              y.isFinite,
              logicalSize.isFinite,
              logicalSize > 0,
              logicalSize <= maximumLogicalFontSize else {
            return text.isEmpty
        }

        let key = atlasKey(family: family, logicalSize: logicalSize, weight: weight)
        guard let atlas = atlas(for: key) else { return false }

        useCounter &+= 1
        atlas.lastUsed = useCounter

        var prepared: [(PreparedGlyph, CGFloat)] = []
        prepared.reserveCapacity(text.unicodeScalars.count)
        var penX: CGFloat = 0

        for scalar in text.unicodeScalars {
            guard let glyph = glyph(for: scalar, atlas: atlas) else {
                // A missing glyph still advances the pen when CoreText knows
                // its width.  Returning false would make a single unusual
                // character disable the entire HUD backend.
                if let advance = missingGlyphAdvance(for: scalar, key: key) {
                    penX += advance
                    continue
                }
                return false
            }
            prepared.append((PreparedGlyph(record: glyph, atlas: atlas), penX))
            penX += glyph.advance
        }

        // A non-empty string must not report success when every glyph failed
        // to rasterize or upload.  The caller uses false to switch to the
        // legacy XPLM font path, which keeps the HUD visible on a graphics
        // context where texture allocation is unavailable.
        guard !prepared.isEmpty else { return false }
        return drawPrepared(prepared, x: x, y: y)
    }

    private func atlasKey(family: String, logicalSize: CGFloat, weight: Int) -> AtlasKey {
        let quarterPoints = max(1, Int((logicalSize * 4.0).rounded()))
        return AtlasKey(
            family: normalizedFamily(family),
            weight: normalizedWeight(weight),
            sizeQuarterPoints: quarterPoints,
            rasterScale: Int(rasterScale)
        )
    }

    private func atlas(for key: AtlasKey) -> Atlas? {
        if let existing = atlases[key] {
            return existing
        }

        if atlases[key] == nil, atlases.count >= maximumAtlases {
            guard let leastUsed = atlases.min(by: { $0.value.lastUsed < $1.value.lastUsed })?.key else {
                return nil
            }
            destroy(atlas: atlases.removeValue(forKey: leastUsed))
        }

        let created = Atlas(key: key)
        guard createTexture(for: created) else { return nil }
        atlases[key] = created
        return created
    }

    private func currentTextureState() -> TextureState {
        var activeTexture: Int32 = Int32(GL_TEXTURE0)
        glGetIntegerv(GLenum(GL_ACTIVE_TEXTURE), &activeTexture)
        glActiveTexture(GLenum(GL_TEXTURE0))
        var textureID: Int32 = 0
        glGetIntegerv(GLenum(GL_TEXTURE_BINDING_2D), &textureID)
        glActiveTexture(GLenum(activeTexture))
        return TextureState(activeTexture: GLenum(activeTexture), textureID: textureID)
    }

    private func bindTextureOnUnitZero(_ textureID: Int32) {
        glActiveTexture(GLenum(GL_TEXTURE0))
        XPLMBindTexture2d(textureID, 0)
    }

    private func restoreTextureState(_ state: TextureState) {
        // glPopAttrib restores the OpenGL binding but not X-Plane's cached
        // binding, so always restore through the SDK helper as well. X-Plane's
        // helper operates on texture unit zero; restore the caller's active
        // unit after updating that cached binding.
        bindTextureOnUnitZero(state.textureID)
        glActiveTexture(state.activeTexture)
    }

    private func pushIdentityTextureMatrix() -> TextureMatrixState {
        var previousMatrixMode: GLint = GLint(GL_MODELVIEW)
        glGetIntegerv(GLenum(GL_MATRIX_MODE), &previousMatrixMode)

        // X-Plane and other plug-ins share the fixed-function matrix stacks.
        // glPushAttrib does not save matrix contents, so an inherited texture
        // matrix can mirror/rotate every glyph while the screen-space quad
        // itself still looks correct.
        glMatrixMode(GLenum(GL_TEXTURE))
        glPushMatrix()
        glLoadIdentity()
        return TextureMatrixState(previousMatrixMode: GLenum(previousMatrixMode))
    }

    private func popTextureMatrix(_ state: TextureMatrixState) {
        glMatrixMode(GLenum(GL_TEXTURE))
        glPopMatrix()
        glMatrixMode(state.previousMatrixMode)
    }

    private func createTexture(for atlas: Atlas) -> Bool {
        var textureID: Int32 = 0
        XPLMGenerateTextureNumbers(&textureID, 1)
        guard textureID != 0 else { return false }

        let previousTextureState = currentTextureState()
        // Save the state before binding.  The attribute stack must include the
        // binding change as well as the sampler parameters.
        glPushAttrib(GLbitfield(GL_TEXTURE_BIT))
        glPushClientAttrib(GLbitfield(GL_CLIENT_PIXEL_STORE_BIT))
        bindTextureOnUnitZero(textureID)
        glPixelStorei(GLenum(GL_UNPACK_ALIGNMENT), 1)
        glPixelStorei(GLenum(GL_UNPACK_ROW_LENGTH), 0)
        glTexParameteri(GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_MIN_FILTER), GLint(GL_LINEAR))
        glTexParameteri(GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_MAG_FILTER), GLint(GL_LINEAR))
        glTexParameteri(GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_WRAP_S), GLint(GL_CLAMP_TO_EDGE))
        glTexParameteri(GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_WRAP_T), GLint(GL_CLAMP_TO_EDGE))
        glTexImage2D(
            GLenum(GL_TEXTURE_2D),
            0,
            GL_ALPHA,
            GLsizei(Atlas.width),
            GLsizei(Atlas.height),
            0,
            GLenum(GL_ALPHA),
            GLenum(GL_UNSIGNED_BYTE),
            nil
        )
        let textureError = glGetError()
        glPopClientAttrib()
        glPopAttrib()
        restoreTextureState(previousTextureState)

        guard textureError == GLenum(GL_NO_ERROR) else {
            var failedTextureID = textureID
            glDeleteTextures(1, &failedTextureID)
            return false
        }

        atlas.textureID = textureID
        return true
    }

    private func destroy(atlas: Atlas?) {
        guard let atlas, atlas.textureID != 0 else { return }
        var textureID = atlas.textureID
        glDeleteTextures(1, &textureID)
    }

    private func glyph(for scalar: Unicode.Scalar, atlas: Atlas) -> GlyphRecord? {
        let key = GlyphKey(scalar: scalar.value)
        if let cached = atlas.glyphs[key] {
            return cached
        }

        let logicalSize = CGFloat(atlas.key.sizeQuarterPoints) / 4.0
        let font = fontForScalar(scalar, family: atlas.key.family, logicalSize: logicalSize, weight: atlas.key.weight)
        let character = String(scalar)
        var utf16 = Array(character.utf16)
        var glyphID: CGGlyph = 0
        let hasGlyph = utf16.withUnsafeMutableBufferPointer { buffer -> Bool in
            guard let baseAddress = buffer.baseAddress else { return false }
            return CTFontGetGlyphsForCharacters(font, baseAddress, &glyphID, 1)
        }
        guard hasGlyph, glyphID != 0 else { return nil }

        var bounds = CGRect.zero
        CTFontGetBoundingRectsForGlyphs(font, .default, &glyphID, &bounds, 1)
        var advance = CGSize.zero
        CTFontGetAdvancesForGlyphs(font, .horizontal, &glyphID, &advance, 1)

        let bitmapWidth = max(1, Int(ceil(bounds.width)) + Atlas.padding * 2)
        let bitmapHeight = max(1, Int(ceil(bounds.height)) + Atlas.padding * 2)
        guard let location = atlasLocation(width: bitmapWidth, height: bitmapHeight, atlas: atlas) else {
            return nil
        }
        guard let pixels = rasterize(font: font,
                                     glyph: glyphID,
                                     bounds: bounds,
                                     width: bitmapWidth,
                                     height: bitmapHeight) else {
            return nil
        }
        guard upload(pixels: pixels,
                     width: bitmapWidth,
                     height: bitmapHeight,
                     x: location.x,
                     y: location.y,
                     atlas: atlas) else {
            return nil
        }

        // The bitmap origin is expressed relative to the baseline in logical
        // boxels.  CoreText's glyph bounds are already baseline-relative.
        let originX = (bounds.minX - CGFloat(Atlas.padding)) / rasterScale
        let originY = (bounds.minY - CGFloat(Atlas.padding)) / rasterScale
        let record = GlyphRecord(
            textureX: location.x,
            textureY: location.y,
            textureWidth: bitmapWidth,
            textureHeight: bitmapHeight,
            originX: originX,
            originY: originY,
            advance: CGFloat(advance.width) / rasterScale
        )
        atlas.glyphs[key] = record
        return record
    }

    private func atlasLocation(width: Int, height: Int, atlas: Atlas) -> (x: Int, y: Int)? {
        guard width <= Atlas.width, height <= Atlas.height else { return nil }

        if atlas.cursorX + width > Atlas.width {
            atlas.cursorX = 0
            atlas.cursorY += atlas.rowHeight
            atlas.rowHeight = 0
        }
        guard atlas.cursorY + height <= Atlas.height else {
            return nil
        }

        let location = (atlas.cursorX, atlas.cursorY)
        atlas.cursorX += width
        atlas.rowHeight = max(atlas.rowHeight, height)
        return location
    }

    private func rasterize(font: CTFont,
                           glyph: CGGlyph,
                           bounds: CGRect,
                           width: Int,
                           height: Int) -> [UInt8]? {
        var pixels = [UInt8](repeating: 0, count: width * height)
        let colorSpace = CGColorSpaceCreateDeviceGray()
        var didRasterize = false
        pixels.withUnsafeMutableBytes { rawBuffer in
            guard let context = CGContext(
                data: rawBuffer.baseAddress,
                width: width,
                height: height,
                bitsPerComponent: 8,
                bytesPerRow: width,
                space: colorSpace,
                bitmapInfo: CGImageAlphaInfo.none.rawValue
            ) else {
                return
            }

            context.setFillColor(gray: 1.0, alpha: 1.0)
            var position = CGPoint(
                x: CGFloat(Atlas.padding) - bounds.minX,
                y: CGFloat(Atlas.padding) - bounds.minY
            )
            var glyphID = glyph
            CTFontDrawGlyphs(font, &glyphID, &position, 1, context)
            didRasterize = true
        }
        return didRasterize ? pixels : nil
    }

    private func upload(pixels: [UInt8], width: Int, height: Int, x: Int, y: Int, atlas: Atlas) -> Bool {
        guard atlas.textureID != 0 else { return false }
        let previousTextureState = currentTextureState()
        glPushAttrib(GLbitfield(GL_TEXTURE_BIT))
        glPushClientAttrib(GLbitfield(GL_CLIENT_PIXEL_STORE_BIT))
        bindTextureOnUnitZero(atlas.textureID)
        glPixelStorei(GLenum(GL_UNPACK_ALIGNMENT), 1)
        glPixelStorei(GLenum(GL_UNPACK_ROW_LENGTH), 0)
        pixels.withUnsafeBytes { rawBuffer in
            glTexSubImage2D(
                GLenum(GL_TEXTURE_2D),
                0,
                GLint(x),
                GLint(y),
                GLsizei(width),
                GLsizei(height),
                GLenum(GL_ALPHA),
                GLenum(GL_UNSIGNED_BYTE),
                rawBuffer.baseAddress
            )
        }
        let uploadError = glGetError()
        glPopClientAttrib()
        glPopAttrib()
        restoreTextureState(previousTextureState)
        return uploadError == GLenum(GL_NO_ERROR)
    }

    private func drawPrepared(_ prepared: [(PreparedGlyph, CGFloat)], x: CGFloat, y: CGFloat) -> Bool {
        let currentColor = currentOpenGLColor()
        guard let firstAtlas = prepared.first?.0.atlas, firstAtlas.textureID != 0 else {
            return false
        }

        let previousTextureState = currentTextureState()
        // Lua and other plug-ins may have just drawn with texturing disabled.
        // X-Plane keeps an internal cache of this state, so use the SDK helper
        // instead of relying on a raw glEnable() alone.  Set the simulator
        // state before binding: XPLMSetGraphicsState may invalidate the
        // currently bound texture on the OpenGL/Metal bridge.
        glPushAttrib(GLbitfield(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT))
        XPLMSetGraphicsState(0, 1, 0, 1, 1, 0, 0)
        // Culling is not part of XPLMSetGraphicsState.  Keep the screen-space
        // quads visible even when the simulator left face culling enabled.
        glDisable(GLenum(GL_CULL_FACE))
        // Keep these explicit as well: X-Plane's state cache can legitimately
        // skip a redundant SDK call while another drawing path has restored
        // the underlying OpenGL state through an attribute stack.
        glEnable(GLenum(GL_TEXTURE_2D))
        glEnable(GLenum(GL_BLEND))
        glBlendFunc(GLenum(GL_SRC_ALPHA), GLenum(GL_ONE_MINUS_SRC_ALPHA))
        glColor4f(currentColor[0], currentColor[1], currentColor[2], currentColor[3])
        glTexEnvi(GLenum(GL_TEXTURE_ENV), GLenum(GL_TEXTURE_ENV_MODE), GLint(GL_MODULATE))
        bindTextureOnUnitZero(firstAtlas.textureID)
        let textureMatrixState = pushIdentityTextureMatrix()

        glBegin(GLenum(GL_QUADS))
        for (preparedGlyph, penX) in prepared {
            let record = preparedGlyph.record
            let left = x + penX + record.originX
            let bottom = y + record.originY
            let right = left + CGFloat(record.textureWidth) / rasterScale
            let top = bottom + CGFloat(record.textureHeight) / rasterScale
            let u0 = Float(record.textureX) / Float(Atlas.width)
            let v0 = Float(record.textureY) / Float(Atlas.height)
            let u1 = Float(record.textureX + record.textureWidth) / Float(Atlas.width)
            let v1 = Float(record.textureY + record.textureHeight) / Float(Atlas.height)

            // CoreGraphics bitmap contexts expose the first byte row at the
            // top of the glyph, whereas OpenGL's v=0 samples the lower edge.
            // Keep U unchanged and swap only V so the glyph is upright.
            glTexCoord2f(u0, v1)
            glVertex2f(GLfloat(left), GLfloat(bottom))
            glTexCoord2f(u1, v1)
            glVertex2f(GLfloat(right), GLfloat(bottom))
            glTexCoord2f(u1, v0)
            glVertex2f(GLfloat(right), GLfloat(top))
            glTexCoord2f(u0, v0)
            glVertex2f(GLfloat(left), GLfloat(top))
        }
        glEnd()
        let drawError = glGetError()
        popTextureMatrix(textureMatrixState)
        glPopAttrib()
        restoreTextureState(previousTextureState)
        XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0)
        return drawError == GLenum(GL_NO_ERROR)
    }

    private func currentOpenGLColor() -> [GLfloat] {
        var color: [GLfloat] = [1, 1, 1, 1]
        color.withUnsafeMutableBufferPointer { buffer in
            guard let baseAddress = buffer.baseAddress else { return }
            glGetFloatv(GLenum(GL_CURRENT_COLOR), baseAddress)
        }
        return color
    }

    private func missingGlyphAdvance(for scalar: Unicode.Scalar, key: AtlasKey) -> CGFloat? {
        let font = fontForScalar(scalar,
                                 family: key.family,
                                 logicalSize: CGFloat(key.sizeQuarterPoints) / 4.0,
                                 weight: key.weight)
        let character = String(scalar) as NSString
        let attributed = NSAttributedString(string: character as String, attributes: [
            NSAttributedString.Key(kCTFontAttributeName as String): font
        ])
        let line = CTLineCreateWithAttributedString(attributed as CFAttributedString)
        let width = CTLineGetTypographicBounds(line, nil, nil, nil)
        return width.isFinite ? CGFloat(width) / rasterScale : nil
    }

    private func fontForScalar(_ scalar: Unicode.Scalar,
                               family: String,
                               logicalSize: CGFloat,
                               weight: Int) -> CTFont {
        let base = makeFont(family: family, logicalSize: logicalSize, weight: weight)
        let string = String(scalar) as CFString
        let fallback = CTFontCreateForString(
            base,
            string,
            CFRange(location: 0, length: CFStringGetLength(string))
        )
        return fallback
    }

    private func makeFont(family: String, logicalSize: CGFloat, weight: Int) -> CTFont {
        let physicalSize = max(1.0, quantizedPhysicalSize(logicalSize))
        let familyName = normalizedFamily(family) == "sf_mono" ? "SF Mono" : "SF Pro Text"
        let normalizedWeight = normalizedWeight(weight)
        let normalizedTrait = min(max((Double(normalizedWeight) - 400.0) / 500.0 * 0.8, -1.0), 1.0)
        let traits: [CFString: Any] = [
            kCTFontWeightTrait: NSNumber(value: normalizedTrait),
        ]
        let attributes: [CFString: Any] = [
            kCTFontFamilyNameAttribute: familyName as CFString,
            kCTFontTraitsAttribute: traits as CFDictionary,
        ]
        let descriptor = CTFontDescriptorCreateWithAttributes(attributes as CFDictionary)
        let font = CTFontCreateWithFontDescriptor(descriptor, physicalSize, nil)
        if normalizedWeight >= 600,
           !CTFontGetSymbolicTraits(font).contains(.traitBold) {
            let boldTraits: CTFontSymbolicTraits = [.traitBold]
            return CTFontCreateCopyWithSymbolicTraits(font,
                                                     physicalSize,
                                                     nil,
                                                     boldTraits,
                                                     boldTraits) ?? font
        }
        return font
    }

    private func quantizedPhysicalSize(_ logicalSize: CGFloat) -> CGFloat {
        let quarterPhysicalPoints = max(1, Int((logicalSize * rasterScale * 4.0).rounded()))
        return CGFloat(quarterPhysicalPoints) / 4.0
    }

    private func normalizedFamily(_ family: String) -> String {
        switch family.lowercased() {
        case "sf_mono", "sfmono", "sf mono":
            return "sf_mono"
        default:
            return "sf_pro_text"
        }
    }

    private func normalizedWeight(_ weight: Int) -> Int {
        min(max(weight, 100), 900)
    }
}
