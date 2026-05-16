import Foundation

/// Swift-friendly utilities for X-Plane.
public enum XPLog {
    /// Logs a message to X-Plane's Log.txt.
    public static func log(_ message: String) {
        let finalMessage = "FlyWithLua-Mac: \(message)\n"
        XPLMDebugString(finalMessage)
    }
}

// Extension to make it even easier to log
public func xpPrint(_ items: Any..., separator: String = " ", terminator: String = "") {
    let output = items.map { "\($0)" }.joined(separator: separator)
    XPLog.log(output + terminator)
}
