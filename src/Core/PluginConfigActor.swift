import Foundation

/// An actor that manages plugin configuration and disk I/O in the background.
public actor PluginConfigActor {
    public static let shared = PluginConfigActor()
    
    private var settings: [String: Any] = [:]
    private let fileURL: URL
    
    private init() {
        guard let appSupport = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else {
            fatalError("Could not find Application Support directory")
        }
        let appDir = appSupport.appendingPathComponent("FlyWithLua-Mac")
        try? FileManager.default.createDirectory(at: appDir, withIntermediateDirectories: true)
        self.fileURL = appDir.appendingPathComponent("settings.plist")
    }
    
    /// Updates a setting in the background.
    public func updateSetting(key: String, value: Any) {
        settings[key] = value
    }
    
    /// Saves settings to disk asynchronously.
    public func saveToDisk() async {
        let data = try? PropertyListSerialization.data(fromPropertyList: settings, format: .xml, options: 0)
        try? data?.write(to: fileURL)
        
        // Log success via the MainActor gateway
        await MainActor.run {
            XPSDKGateway.shared.log("Settings saved to disk.")
        }
    }
}
