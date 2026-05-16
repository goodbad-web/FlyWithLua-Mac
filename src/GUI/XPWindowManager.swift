import AppKit
import SwiftUI

/// Manages the native macOS window for the FlyWithLua-Mac interface.
public final class XPWindowManager: NSObject {
    public static let shared = XPWindowManager()
    
    private var window: NSWindow?
    private var hostingController: NSHostingController<MainView>?
    
    private override init() {
        super.init()
    }
    
    /// Toggles the visibility of the SwiftUI window.
    @objc public func toggleWindow() {
        DispatchQueue.main.async {
            if let window = self.window, window.isVisible {
                window.orderOut(nil)
            } else {
                self.showWindow()
            }
        }
    }
    
    private func showWindow() {
        if window == nil {
            let rootView = MainView()
            let controller = NSHostingController(rootView: rootView)
            self.hostingController = controller
            
            let win = NSWindow(
                contentRect: NSRect(x: 0, y: 0, width: 450, height: 600),
                styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
                backing: .buffered,
                defer: false
            )
            
            win.title = "FlyWithLua-Mac"
            win.titleVisibility = .hidden
            win.titlebarAppearsTransparent = true
            win.isMovableByWindowBackground = true
            win.contentViewController = controller
            win.center()
            win.isReleasedWhenClosed = false
            
            self.window = win
        }
        
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }
}
