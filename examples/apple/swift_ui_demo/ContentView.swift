//
//  ContentView.swift
//  SwiftUI Demo for campello_audio
//
//  Main UI with pattern editor, buttons, sliders, and live status.
//

import SwiftUI

struct ContentView: View {
    @StateObject private var audio = AudioModel()

    var body: some View {
        VStack(spacing: 16) {
            Text("Campello Audio")
                .font(.largeTitle)
                .padding(.top)

            // Pattern Editor
            VStack(alignment: .leading, spacing: 6) {
                Text("Pattern (mini-notation)")
                    .font(.caption)
                    .foregroundColor(.secondary)

                TextEditor(text: $audio.patternText)
                    .font(.system(.body, design: .monospaced))
                    .frame(height: 60)
                    .padding(4)
                    .background(Color.secondary.opacity(0.1))
                    .cornerRadius(6)

                HStack(spacing: 12) {
                    Button(action: { audio.playPattern() }) {
                        Label("Play", systemImage: "play.fill")
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(audio.patternText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)

                    Button(action: { audio.stopPattern() }) {
                        Label("Stop", systemImage: "stop.fill")
                    }
                    .buttonStyle(.bordered)
                    .tint(.red)

                    Spacer()
                }

                if let error = audio.patternError {
                    Text(error)
                        .font(.caption)
                        .foregroundColor(.red)
                        .lineLimit(2)
                }
            }
            .padding(.horizontal)

            Divider()

            // Quick actions
            HStack(spacing: 16) {
                Button("Play Tone") { audio.playTone() }
                    .buttonStyle(.bordered)

                Button("Stop All") { audio.stopAll() }
                    .buttonStyle(.bordered)
                    .tint(.red)
            }

            // Sliders
            VStack(alignment: .leading, spacing: 8) {
                Text("Master Volume: \(Int(audio.masterVolume * 100))%")
                    .font(.caption)
                Slider(value: $audio.masterVolume, in: 0...1)
            }
            .padding(.horizontal)

            VStack(alignment: .leading, spacing: 8) {
                Text("RTPC Intensity: \(Int(audio.intensity * 100))%")
                    .font(.caption)
                Slider(value: $audio.intensity, in: 0...1)
            }
            .padding(.horizontal)

            HStack {
                StatusBadge(label: "Engine", active: audio.isRunning)
                StatusBadge(label: "Voices", value: audio.activeVoices)
            }

            // Help text
            Text("Try: sound(\"<bd sd>\")  or  cat(\"bd*4\", \"sd\")  or  rev(\"bd sd hh\")")
                .font(.caption2)
                .foregroundColor(.secondary)

            Spacer()
        }
        .padding()
        .frame(minWidth: 440, minHeight: 420)
    }
}

// MARK: - Audio Model

class AudioModel: ObservableObject {
    @Published var isRunning = false
    @Published var activeVoices = 0
    @Published var masterVolume: Float = 0.8 {
        didSet { engine.setMasterVolume(masterVolume) }
    }
    @Published var intensity: Float = 0.5 {
        didSet { engine.setParameter("intensity", value: intensity) }
    }
    @Published var patternText = "stack(\"<bd sd>\", \"hh*8\")"
    @Published var patternError: String? = nil

    private let engine = CampelloAudioEngine()
    private var timer: Timer?

    init() {
        isRunning = engine.initialize()
        if isRunning {
            engine.registerParameter("intensity", min: 0.0, max: 1.0)
            engine.setParameter("intensity", value: intensity)
            startPolling()
        }
    }

    deinit {
        timer?.invalidate()
        engine.shutdown()
    }

    func playTone() {
        engine.playTone()
    }

    func playPattern() {
        patternError = nil
        let trimmed = patternText.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { return }
        if let error = engine.compileAndPlayPattern(trimmed) {
            patternError = error
        }
    }

    func stopPattern() {
        engine.stopUserPattern()
    }

    func stopAll() {
        engine.stopAll()
    }

    private func startPolling() {
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            guard let self = self else { return }
            self.activeVoices = Int(self.engine.activeVoiceCount())
        }
    }
}

// MARK: - Status Badge

struct StatusBadge: View {
    var label: String
    var active: Bool? = nil
    var value: Int? = nil

    var body: some View {
        HStack(spacing: 6) {
            Text(label)
                .font(.caption)
                .foregroundColor(.secondary)

            if let active = active {
                Circle()
                    .fill(active ? Color.green : Color.red)
                    .frame(width: 8, height: 8)
            }

            if let value = value {
                Text("\(value)")
                    .font(.caption.monospacedDigit())
                    .foregroundColor(.primary)
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 4)
        .background(Color.secondary.opacity(0.1))
        .cornerRadius(8)
    }
}

// MARK: - Preview

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
