import Foundation
import SwiftUI

private func threeJFPSLocalized(_ english: String, _ japanese: String,
                                state: ThreeJFPSUIState) -> String {
    state.snapshot.resolvedLanguage == "ja" ? japanese : english
}

struct ThreeJFPSSettingsView: View {
    @ObservedObject var state = ThreeJFPSUIState.shared

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("3jFPS12")
                        .font(.title2)
                        .fontWeight(.bold)
                    Text(threeJFPSLocalized("Native M5 Max controller", "M5 Max向けネイティブ制御", state: state))
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                Spacer()
                if state.snapshot.dirty {
                    Text(threeJFPSLocalized("UNSAVED", "未保存", state: state))
                        .font(.caption)
                        .foregroundColor(.orange)
                }
            }
            .padding([.horizontal, .top])

            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    ThreeJFPSStatusCard(state: state)
                    ThreeJFPSControlCard(state: state)
                    ThreeJFPSWizardCard(state: state)
                    ThreeJFPSAdvancedCard(state: state)
                    ThreeJFPSProfilesCard(state: state)
                    ThreeJFPSHUDCard(state: state)
                    ThreeJFPSLanguageCard(state: state)
                    ThreeJFPSSaveBar(state: state)
                }
                .padding()
            }
        }
        .onAppear {
            state.requestSnapshot()
        }
    }
}

private struct ThreeJFPSStatusCard: View {
    @ObservedObject var state: ThreeJFPSUIState

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(threeJFPSLocalized("Live status", "現在の状態", state: state))
                .font(.headline)
            Divider()
            HStack {
                ThreeJFPSMetric(label: "FPS", value: String(format: "%.1f", state.snapshot.fps))
                ThreeJFPSMetric(label: threeJFPSLocalized("Target", "目標", state: state), value: String(format: "%.0f", state.snapshot.targetFPS))
                ThreeJFPSMetric(label: "CPU ms", value: String(format: "%.1f", state.snapshot.cpuMs))
                ThreeJFPSMetric(label: "GPU ms", value: String(format: "%.1f", state.snapshot.gpuMs))
            }
            HStack(spacing: 8) {
                Circle()
                    .fill(statusColor)
                    .frame(width: 9, height: 9)
                Text(controllerReason)
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(.secondary)
                Spacer()
                Text(state.snapshot.cpuDataRefAvailable ? "CPU ✓" : "CPU —")
                    .font(.caption2)
                Text(state.snapshot.gpuDataRefAvailable ? "GPU ✓" : "GPU —")
                    .font(.caption2)
            }
        }
        .padding()
        .background(Color.primary.opacity(0.05))
        .cornerRadius(10)
    }

    private var controllerReason: String {
        switch state.snapshot.controllerReason {
        case "cpu-overload": return threeJFPSLocalized("CPU load", "CPU負荷", state: state)
        case "gpu-overload": return threeJFPSLocalized("GPU load", "GPU負荷", state: state)
        case "cpu-gpu-overload": return threeJFPSLocalized("CPU + GPU load", "CPU + GPU負荷", state: state)
        case "fps-fallback": return threeJFPSLocalized("FPS fallback", "FPSフォールバック", state: state)
        case "recovering": return threeJFPSLocalized("Recovering quality", "品質を回復中", state: state)
        case "off": return threeJFPSLocalized("Controller off", "制御オフ", state: state)
        default: return threeJFPSLocalized("Stable", "安定", state: state)
        }
    }

    private var statusColor: Color {
        if state.snapshot.controllerReason.contains("overload") || state.snapshot.controllerReason == "fps-fallback" {
            return .red
        }
        if state.snapshot.controllerReason == "recovering" {
            return .yellow
        }
        return state.snapshot.mode == "off" ? .gray : .green
    }
}

private struct ThreeJFPSMetric: View {
    let label: String
    let value: String

    var body: some View {
        VStack(spacing: 2) {
            Text(label)
                .font(.caption2)
                .foregroundColor(.secondary)
            Text(value)
                .font(.system(.body, design: .monospaced))
                .fontWeight(.semibold)
        }
        .frame(maxWidth: .infinity)
    }
}

private struct ThreeJFPSControlCard: View {
    @ObservedObject var state: ThreeJFPSUIState

    var body: some View {
        VStack(alignment: .leading, spacing: 9) {
            Text(threeJFPSLocalized("Controller", "制御", state: state))
                .font(.headline)
            Picker(threeJFPSLocalized("Mode", "モード", state: state), selection: modeBinding) {
                Text(threeJFPSLocalized("AUTO", "自動", state: state)).tag("auto")
                Text(threeJFPSLocalized("MAX FPS", "最大FPS", state: state)).tag("max-fps")
                Text(threeJFPSLocalized("MAX QUAL", "最大品質", state: state)).tag("max-quality")
                Text("OFF").tag("off")
            }
            .pickerStyle(MenuPickerStyle())
            Stepper(value: targetBinding, in: 20...120, step: 1) {
                HStack {
                    Text(threeJFPSLocalized("Target FPS", "目標FPS", state: state))
                    Spacer()
                    Text(String(format: "%.0f", state.snapshot.targetFPS))
                        .font(.system(.body, design: .monospaced))
                }
            }
            Stepper(value: cpuHeadroomBinding, in: 0...70, step: 1) {
                HStack {
                    Text(threeJFPSLocalized("CPU headroom", "CPU余裕", state: state))
                    Spacer()
                    Text(String(format: "%.0f%%", cpuHeadroom))
                }
            }
            Stepper(value: gpuHeadroomBinding, in: 0...70, step: 1) {
                HStack {
                    Text(threeJFPSLocalized("GPU headroom", "GPU余裕", state: state))
                    Spacer()
                    Text(String(format: "%.0f%%", gpuHeadroom))
                }
            }
            Text(threeJFPSLocalized("M5 Max defaults: 0.5 s detection, 2 s changes, 30 s recovery.", "M5 Max標準: 検出0.5秒、変更間隔2秒、回復待ち30秒。", state: state))
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .padding()
        .background(Color.primary.opacity(0.05))
        .cornerRadius(10)
    }

    private var modeBinding: Binding<String> {
        Binding(get: { state.snapshot.mode }, set: { state.setMode($0) })
    }

    private var targetBinding: Binding<Double> {
        Binding(get: { state.snapshot.targetFPS }, set: { state.setParameter("Ftg", value: $0) })
    }

    private var cpuHeadroom: Double { state.snapshot.cpuHeadroom }
    private var gpuHeadroom: Double { state.snapshot.gpuHeadroom }

    private var cpuHeadroomBinding: Binding<Double> {
        Binding(get: { state.snapshot.cpuHeadroom }, set: { state.setParameter("CPUhdrm", value: $0) })
    }

    private var gpuHeadroomBinding: Binding<Double> {
        Binding(get: { state.snapshot.gpuHeadroom }, set: { state.setParameter("GPUhdrm", value: $0) })
    }
}

private struct ThreeJFPSWizardCard: View {
    @ObservedObject var state: ThreeJFPSUIState

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(threeJFPSLocalized("Wizard", "ウィザード", state: state))
                    .font(.headline)
                Spacer()
                Text("\(state.wizardStep + 1) / 5")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            Text(stepTitle)
                .font(.subheadline)
            Text(stepDescription)
                .font(.caption)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                if state.wizardStep > 0 {
                    Button(threeJFPSLocalized("Back", "戻る", state: state)) {
                        state.wizardStep -= 1
                    }
                }
                Spacer()
                if state.wizardStep < 4 {
                    Button(threeJFPSLocalized("Next", "次へ", state: state)) {
                        state.wizardStep += 1
                    }
                } else {
                    Button(threeJFPSLocalized("Save wizard settings", "ウィザード設定を保存", state: state)) {
                        state.save()
                    }
                    .buttonStyle(DefaultButtonStyle())
                }
            }
        }
        .padding()
        .background(Color.accentColor.opacity(0.08))
        .cornerRadius(10)
    }

    private var stepTitle: String {
        switch state.wizardStep {
        case 0: return threeJFPSLocalized("Display and target FPS", "表示環境と目標FPS", state: state)
        case 1: return threeJFPSLocalized("AUTO control", "AUTO制御", state: state)
        case 2: return threeJFPSLocalized("LOD", "LOD", state: state)
        case 3: return threeJFPSLocalized("Clouds, shadows and FSR", "雲・影・FSR", state: state)
        default: return threeJFPSLocalized("Review and save", "確認・保存", state: state)
        }
    }

    private var stepDescription: String {
        switch state.wizardStep {
        case 0: return threeJFPSLocalized("Choose the monitor target used by the native controller.", "ネイティブ制御が使うモニター目標FPSを設定します。", state: state)
        case 1: return threeJFPSLocalized("AUTO reacts to CPU/GPU time independently and falls back to FPS when timing DataRefs are unavailable.", "AUTOはCPU/GPU時間を独立して監視し、計測DataRefがなければFPSへフォールバックします。", state: state)
        case 2: return threeJFPSLocalized("Enable LOD only when its DataRef is available.", "LODはDataRefが利用可能な場合だけ有効にします。", state: state)
        case 3: return threeJFPSLocalized("Set the cloud, shadow and FSR feature switches before observing a flight.", "雲・影・FSRの機能スイッチを設定してから飛行中に確認します。", state: state)
        default: return threeJFPSLocalized("SAVE persists through the existing jjjLib1 format. CANCEL reloads the last saved profile.", "SAVEは既存のjjjLib1形式へ保存し、CANCELは最後に保存したプロファイルへ戻します。", state: state)
        }
    }
}

private struct ThreeJFPSAdvancedCard: View {
    @ObservedObject var state: ThreeJFPSUIState

    var body: some View {
        DisclosureGroup(threeJFPSLocalized("Advanced features", "Advanced設定", state: state)) {
            VStack(alignment: .leading, spacing: 8) {
                ForEach(state.snapshot.features) { feature in
                    HStack {
                        VStack(alignment: .leading) {
                            Text(featureLabel(feature.id))
                            Text(feature.available ?
                                 threeJFPSLocalized("Available", "利用可能", state: state) :
                                 threeJFPSLocalized("DataRef unavailable", "DataRefなし", state: state))
                                .font(.caption2)
                                .foregroundColor(.secondary)
                        }
                        Spacer()
                        Text(String(format: "%.2f", feature.current))
                            .font(.system(.caption, design: .monospaced))
                        if feature.manualOverride {
                            Text(threeJFPSLocalized("MANUAL", "手動", state: state))
                                .font(.caption2)
                                .foregroundColor(.orange)
                        }
                        Toggle("", isOn: featureBinding(feature))
                            .labelsHidden()
                            .accessibilityLabel(featureLabel(feature.id))
                            .disabled(!feature.available)
                    }
                    .padding(.vertical, 2)
                }
                Button(threeJFPSLocalized("Clear session overrides", "セッション固定を解除", state: state)) {
                    state.clearFeatureOverride()
                }
                .font(.caption)
            }
            .padding(.top, 8)
        }
        .padding()
        .background(Color.primary.opacity(0.05))
        .cornerRadius(10)
    }

    private func featureLabel(_ id: String) -> String {
        switch id {
        case "lod": return "LOD"
        case "shadows": return threeJFPSLocalized("Far shadows", "遠方影", state: state)
        case "clouds": return threeJFPSLocalized("Clouds", "雲", state: state)
        case "fsr": return "FSR"
        default: return id
        }
    }

    private func featureBinding(_ feature: ThreeJFPSFeatureSnapshot) -> Binding<Bool> {
        Binding(
            get: { state.snapshot.features.first(where: { $0.id == feature.id })?.enabled ?? false },
            set: { state.setParameter(parameterName(feature.id), value: $0) }
        )
    }

    private func parameterName(_ id: String) -> String {
        switch id {
        case "lod": return "LDa"
        case "shadows": return "ShDa"
        case "clouds": return "CLDa"
        case "fsr": return "FSRa"
        default: return id
        }
    }
}

private struct ThreeJFPSProfilesCard: View {
    @ObservedObject var state: ThreeJFPSUIState

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(threeJFPSLocalized("Profiles", "プロファイル", state: state))
                .font(.headline)
            Picker(threeJFPSLocalized("Current profile", "現在のプロファイル", state: state), selection: profileBinding) {
                ForEach(["A", "B", "C", "D"], id: \.self) { profile in
                    Text(profile).tag(profile)
                }
            }
            .pickerStyle(SegmentedPickerStyle())
        }
        .padding()
        .background(Color.primary.opacity(0.05))
        .cornerRadius(10)
    }

    private var profileBinding: Binding<String> {
        Binding(get: { state.snapshot.profile }, set: { state.setProfile($0) })
    }
}

private struct ThreeJFPSHUDCard: View {
    @ObservedObject var state: ThreeJFPSUIState

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(threeJFPSLocalized("HUD editing", "HUD編集", state: state))
                    .font(.headline)
                Spacer()
                Toggle("", isOn: editingBinding)
                    .labelsHidden()
            }
            Text(state.snapshot.hudEditing ?
                 threeJFPSLocalized("Drag the HUD to move it; scroll to resize. Turn this off to return to normal click-to-open behavior.", "HUDをドラッグして移動、スクロールでサイズ変更します。通常クリックで設定を開くにはオフに戻します。", state: state) :
                 threeJFPSLocalized("Normal click opens this settings window. Moving and resizing require edit mode.", "通常クリックで設定画面を開きます。移動・リサイズには編集モードが必要です。", state: state))
                .font(.caption)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding()
        .background(Color.primary.opacity(0.05))
        .cornerRadius(10)
    }

    private var editingBinding: Binding<Bool> {
        Binding(get: { state.snapshot.hudEditing }, set: { state.setHUDEditing($0) })
    }
}

private struct ThreeJFPSLanguageCard: View {
    @ObservedObject var state: ThreeJFPSUIState

    var body: some View {
        Picker(threeJFPSLocalized("Language", "言語", state: state), selection: languageBinding) {
            Text("Auto").tag("auto")
            Text("English").tag("en")
            Text("日本語").tag("ja")
        }
        .pickerStyle(SegmentedPickerStyle())
    }

    private var languageBinding: Binding<String> {
        Binding(get: { state.snapshot.language }, set: { state.setLanguage($0) })
    }
}

private struct ThreeJFPSSaveBar: View {
    @ObservedObject var state: ThreeJFPSUIState

    var body: some View {
        HStack {
            Button(threeJFPSLocalized("CANCEL", "キャンセル", state: state)) {
                state.cancel()
            }
            Spacer()
            Button(threeJFPSLocalized("Defaults", "標準値", state: state)) {
                state.restoreDefaults()
            }
            Button(threeJFPSLocalized("SAVE", "保存", state: state)) {
                state.save()
            }
            .buttonStyle(DefaultButtonStyle())
        }
    }
}
