import SwiftUI
import Combine

/// Manages the global state for the FlyWithLua-Mac UI.
public final class XPUIState: ObservableObject {
    public static let shared = XPUIState()
    
    @Published public var isPluginEnabled: Bool = true
    @Published public var lastLogMessage: String = "Welcome to FlyWithLua-Mac"
    @Published public var scriptCount: Int = 0
    
    // Example DataRef binding
    private var altitudeRef = DataRef<Double>("sim/flightmodel/position/elevation")
    @Published public var currentAltitude: Double = 0.0
    
    private init() {
        // Start a timer to poll data from X-Plane
        Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            self?.updateState()
        }
    }
    
    public func updateState() {
        DispatchQueue.main.async {
            if let alt = self.altitudeRef?.value {
                self.currentAltitude = alt
            }
        }
    }
}
