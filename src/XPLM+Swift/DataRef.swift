import Foundation

/// A type-safe wrapper for X-Plane DataRefs.
public final class DataRef<T> {
    public let name: String
    private let ref: XPLMDataRef
    
    public init?(_ name: String) {
        guard let foundRef = XPLMFindDataRef(name) else {
            return nil
        }
        self.name = name
        self.ref = foundRef
    }
    
    public var value: T? {
        get {
            if T.self == Float.self {
                return XPLMGetDataf(ref) as? T
            } else if T.self == Double.self {
                return XPLMGetDatad(ref) as? T
            } else if T.self == Int.self {
                return Int(XPLMGetDatai(ref)) as? T
            } else if T.self == Bool.self {
                return (XPLMGetDatai(ref) != 0) as? T
            }
            return nil
        }
        set {
            guard let newValue = newValue else { return }
            if T.self == Float.self {
                XPLMSetDataf(ref, newValue as! Float)
            } else if T.self == Double.self {
                XPLMSetDatad(ref, newValue as! Double)
            } else if T.self == Int.self {
                XPLMSetDatai(ref, Int32(newValue as! Int))
            } else if T.self == Bool.self {
                XPLMSetDatai(ref, (newValue as! Bool) ? 1 : 0)
            }
        }
    }
}

enum LuaDataValue {
    case number(Double)
    case integer(Int32)
    case boolean(Bool)
    case string(Data)

    var numberValue: Double {
        switch self {
        case .number(let value):
            return value
        case .integer(let value):
            return Double(value)
        case .boolean(let value):
            return value ? 1.0 : 0.0
        case .string:
            return 0.0
        }
    }

    func push(to L: OpaquePointer?) {
        switch self {
        case .number(let value):
            lua_pushnumber(L, value)
        case .integer(let value):
            lua_pushinteger(L, lua_Integer(value))
        case .boolean(let value):
            lua_pushboolean(L, value ? 1 : 0)
        case .string(let value):
            value.withUnsafeBytes { buffer in
                if let baseAddress = buffer.baseAddress {
                    lua_pushlstring(L, baseAddress.assumingMemoryBound(to: CChar.self), value.count)
                } else {
                    lua_pushlstring(L, "", 0)
                }
            }
        }
    }
}

private func flywithluaLog(_ message: String) {
    XPLMDebugString("FlyWithLua-Mac: " + message + "\n")
}

private final class LuaDataRef {
    let path: String
    private let ref: XPLMDataRef
    private let typeMask: XPLMDataTypeID

    init?(_ path: String) {
        guard let foundRef = XPLMFindDataRef(path) else {
            return nil
        }

        self.path = path
        self.ref = foundRef
        self.typeMask = XPLMGetDataRefTypes(foundRef)
    }

    var canWrite: Bool {
        XPLMCanWriteDataRef(ref) != 0
    }

    private func hasType(_ type: XPLMDataTypeID) -> Bool {
        (typeMask & type) != 0
    }

    func read(index: Int?) -> LuaDataValue? {
        if hasType(XPLMDataTypeID(xplmType_Data)) {
            return readData(offset: index ?? 0)
        }

        if let index = index {
            if let arrayValue = readArray(index: index) {
                return arrayValue
            }

            if index == 0 {
                return readScalar()
            }

            return nil
        }

        if let scalar = readScalar() {
            return scalar
        }

        return readArray(index: 0)
    }

    func write(_ value: LuaDataValue, index: Int?) -> Bool {
        guard canWrite else {
            return false
        }

        if case .string(let bytes) = value {
            if let index = index {
                return writeData(bytes, offset: index)
            }

            return writeData(bytes, offset: 0)
        }

        if let index = index {
            if writeArray(value, index: index) {
                return true
            }

            if index == 0 {
                return writeScalar(value)
            }

            return false
        }

        if writeScalar(value) {
            return true
        }

        return writeArray(value, index: 0)
    }

    private func readScalar() -> LuaDataValue? {
        if hasType(XPLMDataTypeID(xplmType_Double)) {
            return .number(XPLMGetDatad(ref))
        }

        if hasType(XPLMDataTypeID(xplmType_Float)) {
            return .number(Double(XPLMGetDataf(ref)))
        }

        if hasType(XPLMDataTypeID(xplmType_Int)) {
            return .integer(Int32(XPLMGetDatai(ref)))
        }

        if hasType(XPLMDataTypeID(xplmType_FloatArray)) {
            var value: Float = 0
            let copied = XPLMGetDatavf(ref, &value, 0, 1)
            if copied > 0 {
                return .number(Double(value))
            }
        }

        if hasType(XPLMDataTypeID(xplmType_IntArray)) {
            var value: Int32 = 0
            let copied = XPLMGetDatavi(ref, &value, 0, 1)
            if copied > 0 {
                return .integer(value)
            }
        }

        return nil
    }

    private func readData(offset: Int) -> LuaDataValue? {
        let totalBytes = Int(XPLMGetDatab(ref, nil, 0, 0))
        if totalBytes < 0 {
            return nil
        }

        if offset > totalBytes {
            return nil
        }

        let bytesToRead = max(totalBytes - offset, 0)
        if bytesToRead == 0 {
            return .string(Data())
        }

        var buffer = [UInt8](repeating: 0, count: bytesToRead)
        let copied = buffer.withUnsafeMutableBytes { rawBuffer -> Int in
            guard let baseAddress = rawBuffer.baseAddress else {
                return 0
            }
            return Int(XPLMGetDatab(ref, baseAddress, Int32(offset), Int32(bytesToRead)))
        }

        if copied < 0 {
            return nil
        }

        return .string(Data(buffer.prefix(copied)))
    }

    private func readArray(index: Int) -> LuaDataValue? {
        if hasType(XPLMDataTypeID(xplmType_IntArray)) {
            var value: Int32 = 0
            let copied = XPLMGetDatavi(ref, &value, Int32(index), 1)
            if copied > 0 {
                return .integer(value)
            }
        }

        if hasType(XPLMDataTypeID(xplmType_FloatArray)) {
            var value: Float = 0
            let copied = XPLMGetDatavf(ref, &value, Int32(index), 1)
            if copied > 0 {
                return .number(Double(value))
            }
        }

        return nil
    }

    private func writeScalar(_ value: LuaDataValue) -> Bool {
        if hasType(XPLMDataTypeID(xplmType_Double)) {
            XPLMSetDatad(ref, value.numberValue)
            return true
        }

        if hasType(XPLMDataTypeID(xplmType_Float)) {
            XPLMSetDataf(ref, Float(value.numberValue))
            return true
        }

        if hasType(XPLMDataTypeID(xplmType_Int)) {
            XPLMSetDatai(ref, Int32(value.numberValue))
            return true
        }

        return false
    }

    private func writeArray(_ value: LuaDataValue, index: Int) -> Bool {
        if hasType(XPLMDataTypeID(xplmType_IntArray)) {
            var intValue = Int32(value.numberValue)
            XPLMSetDatavi(ref, &intValue, Int32(index), 1)
            return true
        }

        if hasType(XPLMDataTypeID(xplmType_FloatArray)) {
            var floatValue = Float(value.numberValue)
            XPLMSetDatavf(ref, &floatValue, Int32(index), 1)
            return true
        }

        return false
    }

    private func writeData(_ value: Data, offset: Int) -> Bool {
        var terminatedValue = value
        terminatedValue.append(0)

        return terminatedValue.withUnsafeBytes { rawBuffer in
            guard let baseAddress = rawBuffer.baseAddress else {
                return false
            }

            XPLMSetDatab(ref, UnsafeMutableRawPointer(mutating: baseAddress), Int32(offset), Int32(terminatedValue.count))
            return true
        }
    }
}

private var luaDataRefCache: [String: LuaDataRef] = [:]

private func cachedLuaDataRef(_ path: String) -> LuaDataRef? {
    if let cached = luaDataRefCache[path] {
        return cached
    }

    guard let dataRef = LuaDataRef(path) else {
        return nil
    }

    luaDataRefCache[path] = dataRef
    return dataRef
}

func flywithluaReadDataRef(path: String, index: Int?) -> LuaDataValue? {
    cachedLuaDataRef(path)?.read(index: index)
}

func flywithluaWriteDataRef(path: String, value: LuaDataValue, index: Int?) -> Bool {
    guard let dataRef = cachedLuaDataRef(path) else {
        flywithluaLog("DataRef not found: " + path)
        return false
    }

    guard dataRef.canWrite else {
        flywithluaLog("DataRef is readonly: " + path)
        return false
    }

    guard dataRef.write(value, index: index) else {
        flywithluaLog("Unsupported DataRef type for write: " + path)
        return false
    }

    return true
}

func flywithluaLogDataRefIssue(_ message: String) {
    flywithluaLog(message)
}
