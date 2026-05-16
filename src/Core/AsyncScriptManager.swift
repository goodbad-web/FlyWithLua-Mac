import Foundation

/// Manages asynchronous loading of Lua scripts.
@MainActor
public final class AsyncScriptManager {
    public static let shared = AsyncScriptManager()
    
    private init() {}
    
    /// Loads a script file from disk asynchronously without blocking the main thread.
    public func loadScriptAsync(url: URL) async {
        XPSDKGateway.shared.log("Starting async load: \(url.lastPathComponent)")
        
        do {
            // Background reading
            let content = try await Task.detached(priority: .background) {
                return try String(contentsOf: url, encoding: .utf8)
            }.value
            
            // Back on MainActor, execute the script in Lua
            self.executeInLua(content: content, name: url.lastPathComponent)
            
        } catch {
            XPSDKGateway.shared.log("Error loading script: \(error.localizedDescription)")
        }
    }
    
    private func executeInLua(content: String, name: String) {
        // Here we would call the C++ bridge to actually run the code in the Lua state
        XPSDKGateway.shared.log("Script \(name) ready for execution.")
    }
}
