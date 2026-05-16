import Foundation

/// A thread-safe gateway to the X-Plane SDK.
/// All methods are isolated to the @MainActor to ensure thread safety.
@MainActor
public final class XPSDKGateway {
    public static let shared = XPSDKGateway()
    
    private init() {}
    
    /// Safely writes to X-Plane's Log.txt.
    public func log(_ message: String) {
        XPLMDebugString("FlyWithLua-Mac [MainActor]: \(message)\n")
    }
    
    /// Finds a DataRef safely on the main actor.
    public func findDataRef(_ name: String) -> XPLMDataRef? {
        return XPLMFindDataRef(name)
    }
    
    /// Retrieves a double value from a DataRef.
    public func getDataRefValue(_ ref: XPLMDataRef) -> Double {
        return XPLMGetDatad(ref)
    }
    
    /// Sets a double value to a DataRef.
    public func setDataRefValue(_ ref: XPLMDataRef, value: Double) {
        XPLMSetDatad(ref, value)
    }
}
