import SwiftUI
import Combine

/// Manages the global state for the FlyWithLua-Mac UI.
public final class XPUIState: ObservableObject {
    public static let shared = XPUIState()

    public struct ScriptLoadFailure: Identifiable, Hashable {
        public let id = UUID()
        public let fileName: String
        public let message: String
    }
    
    @Published public var isPluginEnabled: Bool = true
    @Published public var lastLogMessage: String = "Welcome to FlyWithLua-Mac"
    @Published public var scriptCount: Int = 0
    @Published public var scriptDiscoveredCount: Int = 0
    @Published public var scriptLoadedCount: Int = 0
    @Published public var scriptFailedCount: Int = 0
    @Published public var currentAltitude: Double = 0.0
    @Published public var scriptLoadFailures: [ScriptLoadFailure] = []
    
    private init() {}

    public func clearScriptLoadFailures() {
        if Thread.isMainThread {
            scriptLoadFailures.removeAll()
        } else {
            DispatchQueue.main.async { [weak self] in
                self?.scriptLoadFailures.removeAll()
            }
        }
    }

    public func updateScriptLoadFailures(_ failures: [ScriptLoadFailure]) {
        if Thread.isMainThread {
            scriptLoadFailures = failures
        } else {
            DispatchQueue.main.async { [weak self] in
                self?.scriptLoadFailures = failures
            }
        }
    }

    public func updateCurrentAltitude(_ altitude: Double) {
        if Thread.isMainThread {
            currentAltitude = altitude
        } else {
            DispatchQueue.main.async { [weak self] in
                self?.currentAltitude = altitude
            }
        }
    }

    public func updateScriptCount(_ count: Int) {
        if Thread.isMainThread {
            scriptCount = count
            scriptLoadedCount = count
        } else {
            DispatchQueue.main.async { [weak self] in
                self?.scriptCount = count
                self?.scriptLoadedCount = count
            }
        }
    }

    public func updateScriptLoadSummary(discovered: Int, loaded: Int, failed: Int, failures: [ScriptLoadFailure]) {
        let safeDiscovered = max(0, discovered)
        let safeLoaded = max(0, loaded)
        let safeFailed = max(0, failed)
        if Thread.isMainThread {
            scriptDiscoveredCount = safeDiscovered
            scriptLoadedCount = safeLoaded
            scriptCount = safeLoaded
            scriptFailedCount = safeFailed
            scriptLoadFailures = failures
        } else {
            DispatchQueue.main.async { [weak self] in
                self?.scriptDiscoveredCount = safeDiscovered
                self?.scriptLoadedCount = safeLoaded
                self?.scriptCount = safeLoaded
                self?.scriptFailedCount = safeFailed
                self?.scriptLoadFailures = failures
            }
        }
    }

    public func resetScriptLoadSummary() {
        updateScriptLoadSummary(discovered: 0, loaded: 0, failed: 0, failures: [])
    }

    public func updateLastLogMessage(_ message: String) {
        if Thread.isMainThread {
            lastLogMessage = message
        } else {
            DispatchQueue.main.async { [weak self] in
                self?.lastLogMessage = message
            }
        }
    }
}
