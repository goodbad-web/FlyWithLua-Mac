import SwiftUI

struct MainView: View {
    @ObservedObject var state = XPUIState.shared
    
    var body: some View {
        VStack(spacing: 20) {
            HeaderView()
            
            StatusCard()
            
            LogView()
            
            Spacer()
            
            FooterView()
        }
        .padding()
        .frame(minWidth: 400, minHeight: 500)
        .background(VisualEffectView(material: .hudWindow, blendingMode: .behindWindow).ignoresSafeArea())
    }
}

struct HeaderView: View {
    var body: some View {
        HStack {
            Image(systemName: "airplane.circle.fill")
                .font(.system(size: 40))
                .foregroundColor(.accentColor)
            
            VStack(alignment: .leading) {
                Text("FlyWithLua-Mac")
                    .font(.title)
                    .fontWeight(.bold)
                Text("Native Apple Silicon Edition")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            
            Spacer()
        }
    }
}

struct StatusCard: View {
    @ObservedObject var state = XPUIState.shared
    
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label("Status", systemImage: "info.circle")
                .font(.headline)
            
            Divider()
            
            HStack {
                StatusItem(label: "Altitude", value: String(format: "%.0f ft", state.currentAltitude))
                Divider().frame(height: 30)
                StatusItem(label: "Scripts", value: "\(state.scriptCount)")
                Divider().frame(height: 30)
                StatusItem(label: "State", value: state.isPluginEnabled ? "Active" : "Idle", color: state.isPluginEnabled ? .green : .red)
            }
        }
        .padding()
        .background(Color.primary.opacity(0.05))
        .cornerRadius(12)
    }
}

struct StatusItem: View {
    var label: String
    var value: String
    var color: Color = .primary
    
    var body: some View {
        VStack {
            Text(label)
                .font(.caption2)
                .foregroundColor(.secondary)
            Text(value)
                .font(.system(.body, design: .monospaced))
                .fontWeight(.bold)
                .foregroundColor(color)
        }
        .frame(maxWidth: .infinity)
    }
}

struct LogView: View {
    @ObservedObject var state = XPUIState.shared
    
    var body: some View {
        VStack(alignment: .leading) {
            Text("Latest Message")
                .font(.caption)
                .foregroundColor(.secondary)
            
            Text(state.lastLogMessage)
                .font(.system(.body, design: .monospaced))
                .padding()
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Color.black.opacity(0.2))
                .cornerRadius(8)
        }
    }
}

struct FooterView: View {
    var body: some View {
        HStack {
            Button(action: {
                // Reload logic
            }) {
                Label("Reload Scripts", systemImage: "arrow.clockwise")
                    .padding(.horizontal, 10)
                    .padding(.vertical, 5)
                    .background(Color.accentColor)
                    .foregroundColor(.white)
                    .cornerRadius(6)
            }
            .buttonStyle(PlainButtonStyle())
            
            Spacer()
            
            Text("v2.8.0-Native")
                .font(.system(size: 10))
                .foregroundColor(.secondary)
        }
    }
}

// Helper for Background Blur
struct VisualEffectView: NSViewRepresentable {
    var material: NSVisualEffectView.Material
    var blendingMode: NSVisualEffectView.BlendingMode
    
    func makeNSView(context: Context) -> NSVisualEffectView {
        let view = NSVisualEffectView()
        view.material = material
        view.blendingMode = blendingMode
        view.state = .active
        return view
    }
    
    func updateNSView(_ nsView: NSVisualEffectView, context: Context) {
        nsView.material = material
        nsView.blendingMode = blendingMode
    }
}
