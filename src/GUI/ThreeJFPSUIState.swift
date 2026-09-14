import Foundation
import Combine

public struct ThreeJFPSFeatureSnapshot: Codable, Identifiable, Hashable {
    public let id: String
    public let enabled: Bool
    public let available: Bool
    public let current: Double
    public let min: Double
    public let max: Double
    public let manualOverride: Bool

    enum CodingKeys: String, CodingKey {
        case id, enabled, available, current, min, max, manualOverride
    }
}

public struct ThreeJFPSSnapshot: Codable, Equatable {
    public var schemaVersion: Int = 1
    public var mode: String = "auto"
    public var profile: String = "A"
    public var fps: Double = 0
    public var targetFPS: Double = 30
    public var cpuMs: Double = 0
    public var gpuMs: Double = 0
    public var cpuHeadroom: Double = 20
    public var gpuHeadroom: Double = 10
    public var detectionWindow: Double = 0.5
    public var changeInterval: Double = 2
    public var recoveryDelay: Double = 30
    public var limiter: String = "none"
    public var controllerReason: String = "starting"
    public var cpuDataRefAvailable: Bool = false
    public var gpuDataRefAvailable: Bool = false
    public var dirty: Bool = false
    public var language: String = "auto"
    public var resolvedLanguage: String = "en"
    public var hudEditing: Bool = false
    public var showGraph: Bool = true
    public var showUtilisation: Bool = true
    public var dataRefAvailability: [String: Bool] = [:]
    public var features: [ThreeJFPSFeatureSnapshot] = []

    public init() {}

    enum CodingKeys: String, CodingKey {
        case schemaVersion, mode, profile, fps, targetFPS, cpuMs, gpuMs
        case cpuHeadroom, gpuHeadroom, detectionWindow, changeInterval, recoveryDelay
        case limiter, controllerReason, cpuDataRefAvailable, gpuDataRefAvailable
        case dirty, language, resolvedLanguage, hudEditing, showGraph, showUtilisation
        case dataRefAvailability, features
    }

    public init(from decoder: Decoder) throws {
        self.init()
        let container = try decoder.container(keyedBy: CodingKeys.self)
        schemaVersion = try container.decodeIfPresent(Int.self, forKey: .schemaVersion) ?? schemaVersion
        mode = try container.decodeIfPresent(String.self, forKey: .mode) ?? mode
        profile = try container.decodeIfPresent(String.self, forKey: .profile) ?? profile
        fps = try container.decodeIfPresent(Double.self, forKey: .fps) ?? fps
        targetFPS = try container.decodeIfPresent(Double.self, forKey: .targetFPS) ?? targetFPS
        cpuMs = try container.decodeIfPresent(Double.self, forKey: .cpuMs) ?? cpuMs
        gpuMs = try container.decodeIfPresent(Double.self, forKey: .gpuMs) ?? gpuMs
        cpuHeadroom = try container.decodeIfPresent(Double.self, forKey: .cpuHeadroom) ?? cpuHeadroom
        gpuHeadroom = try container.decodeIfPresent(Double.self, forKey: .gpuHeadroom) ?? gpuHeadroom
        detectionWindow = try container.decodeIfPresent(Double.self, forKey: .detectionWindow) ?? detectionWindow
        changeInterval = try container.decodeIfPresent(Double.self, forKey: .changeInterval) ?? changeInterval
        recoveryDelay = try container.decodeIfPresent(Double.self, forKey: .recoveryDelay) ?? recoveryDelay
        limiter = try container.decodeIfPresent(String.self, forKey: .limiter) ?? limiter
        controllerReason = try container.decodeIfPresent(String.self, forKey: .controllerReason) ?? controllerReason
        cpuDataRefAvailable = try container.decodeIfPresent(Bool.self, forKey: .cpuDataRefAvailable) ?? cpuDataRefAvailable
        gpuDataRefAvailable = try container.decodeIfPresent(Bool.self, forKey: .gpuDataRefAvailable) ?? gpuDataRefAvailable
        dirty = try container.decodeIfPresent(Bool.self, forKey: .dirty) ?? dirty
        language = try container.decodeIfPresent(String.self, forKey: .language) ?? language
        resolvedLanguage = try container.decodeIfPresent(String.self, forKey: .resolvedLanguage) ?? resolvedLanguage
        hudEditing = try container.decodeIfPresent(Bool.self, forKey: .hudEditing) ?? hudEditing
        showGraph = try container.decodeIfPresent(Bool.self, forKey: .showGraph) ?? showGraph
        showUtilisation = try container.decodeIfPresent(Bool.self, forKey: .showUtilisation) ?? showUtilisation
        dataRefAvailability = try container.decodeIfPresent([String: Bool].self, forKey: .dataRefAvailability) ?? dataRefAvailability
        features = try container.decodeIfPresent([ThreeJFPSFeatureSnapshot].self, forKey: .features) ?? features
    }
}

/// Semantic state shared by the native 3jFPS12 runtime and SwiftUI.
public final class ThreeJFPSUIState: ObservableObject {
    public static let shared = ThreeJFPSUIState()

    @Published public private(set) var snapshot = ThreeJFPSSnapshot()
    @Published public var wizardStep = 0

    private init() {}

    public func updateSnapshot(_ payload: String) {
        guard let data = payload.data(using: .utf8) else { return }
        do {
            let decoded = try JSONDecoder().decode(ThreeJFPSSnapshot.self, from: data)
            updateSnapshot(decoded)
        } catch {
            // A malformed snapshot should not take down the host UI. Keep the
            // last known state and leave diagnostics to the native log.
        }
    }

    public func updateSnapshot(_ newSnapshot: ThreeJFPSSnapshot) {
        if Thread.isMainThread {
            snapshot = newSnapshot
        } else {
            DispatchQueue.main.async { [weak self] in
                self?.snapshot = newSnapshot
            }
        }
    }

    public func requestSnapshot() {
        send(["type": "requestSnapshot"])
    }

    public func setMode(_ mode: String) {
        send(["type": "setMode", "value": mode])
    }

    public func setProfile(_ profile: String) {
        send(["type": "setProfile", "value": profile])
    }

    public func setLanguage(_ language: String) {
        send(["type": "setLanguage", "value": language])
    }

    public func setParameter(_ key: String, value: Any) {
        send(["type": "setParameter", "key": key, "value": value])
    }

    public func setHUDEditing(_ enabled: Bool) {
        send(["type": "setHUDEditing", "value": enabled])
    }

    public func setShowGraph(_ enabled: Bool) {
        send(["type": "setGraph", "value": enabled])
    }

    public func setFeatureOverride(_ id: String, value: Double) {
        send(["type": "setFeatureOverride", "id": id, "value": value])
    }

    public func clearFeatureOverride() {
        send(["type": "clearFeatureOverride"])
    }

    public func save() {
        send(["type": "save"])
    }

    public func cancel() {
        send(["type": "cancel"])
    }

    public func restoreDefaults() {
        send(["type": "defaults"])
    }

    private func send(_ command: [String: Any]) {
        guard JSONSerialization.isValidJSONObject(command),
              let data = try? JSONSerialization.data(withJSONObject: command),
              let json = String(data: data, encoding: .utf8) else {
            return
        }
        json.withCString { pointer in
            threejfps_enqueue_command(pointer)
        }
    }
}
