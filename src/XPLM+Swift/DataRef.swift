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
