import SwiftUI

private func flyWithLuaLocalized(_ english: String, _ japanese: String) -> String {
    let languageCode = Locale.preferredLanguages.first?.split(separator: "-").first
    return languageCode == "ja" ? japanese : english
}

struct MainView: View {
    @ObservedObject var state = XPUIState.shared
    
    var body: some View {
        VStack(spacing: 20) {
            HeaderView()
            
            StatusCard()
            
            LogView()

            ScriptFailureView()
            
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
            Label(flyWithLuaLocalized("Status", "状態"), systemImage: "info.circle")
                .font(.headline)
            
            Divider()
            
            HStack {
                StatusItem(label: flyWithLuaLocalized("Altitude", "高度"), value: String(format: "%.0f ft", state.currentAltitude))
                Divider().frame(height: 30)
                StatusItem(label: flyWithLuaLocalized("Scripts", "スクリプト"), value: "\(state.scriptLoadedCount) / \(state.scriptDiscoveredCount)")
                Divider().frame(height: 30)
                StatusItem(label: flyWithLuaLocalized("Failures", "失敗"), value: "\(state.scriptFailedCount)", color: state.scriptFailedCount == 0 ? .green : .orange)
                Divider().frame(height: 30)
                StatusItem(label: flyWithLuaLocalized("State", "状態"), value: state.isPluginEnabled ? flyWithLuaLocalized("Active", "有効") : flyWithLuaLocalized("Idle", "待機"), color: state.isPluginEnabled ? .green : .red)
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
            Text(flyWithLuaLocalized("Latest Message", "最新メッセージ"))
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

struct ScriptFailureView: View {
    @ObservedObject var state = XPUIState.shared

    var body: some View {
        if state.scriptLoadFailures.isEmpty {
            EmptyView()
        } else {
            VStack(alignment: .leading, spacing: 10) {
                Label(flyWithLuaLocalized("Script Load Failures (\(state.scriptLoadFailures.count))", "スクリプト読み込み失敗 (\(state.scriptLoadFailures.count))"), systemImage: "exclamationmark.triangle.fill")
                    .font(.headline)
                    .foregroundColor(.orange)

                Divider()

                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 8) {
                        ForEach(state.scriptLoadFailures) { failure in
                            VStack(alignment: .leading, spacing: 4) {
                                Text(failure.fileName)
                                    .font(.system(.body, design: .monospaced))
                                    .fontWeight(.semibold)
                                    .foregroundColor(.primary)
                                    .lineLimit(2)

                                Text(failure.message)
                                    .font(.system(.caption, design: .monospaced))
                                    .foregroundColor(.secondary)
                                    .lineLimit(4)
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(10)
                            .background(Color.red.opacity(0.08))
                            .cornerRadius(8)
                        }
                    }
                }
                .frame(maxHeight: 160)
            }
            .padding()
            .background(Color.primary.opacity(0.05))
            .cornerRadius(12)
        }
    }
}

struct FooterView: View {
    var body: some View {
        HStack {
            Button(action: {
                XPUIState.shared.resetScriptLoadSummary()
                XPUIState.shared.updateLastLogMessage("Reloading scripts...")
                flywithlua_reload_scripts()
            }) {
                Label(flyWithLuaLocalized("Reload Scripts", "スクリプトを再読み込み"), systemImage: "arrow.clockwise")
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
