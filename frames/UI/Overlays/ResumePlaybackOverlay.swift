import SwiftUI
import AppKit

public struct ResumePromptData: Identifiable, Sendable {
    public let id = UUID()
    public let url: URL
    public let resumeSeconds: Double
    public let formattedTime: String
    public let formattedDuration: String
    public let progress: Double

    public var title: String { url.deletingPathExtension().lastPathComponent }
    public var format: String { url.pathExtension.uppercased() }

    public init(url: URL, resumeSeconds: Double, formattedTime: String, formattedDuration: String, progress: Double) {
        self.url = url
        self.resumeSeconds = resumeSeconds
        self.formattedTime = formattedTime
        self.formattedDuration = formattedDuration
        self.progress = progress
    }
}

public struct ResumePlaybackOverlay: View {
    public let prompt: ResumePromptData
    public var onResume: (URL, Double) -> Void
    public var onStartOver: (URL) -> Void
    public var onCancel: () -> Void

    @State private var isHoveringResume: Bool = false
    @State private var isHoveringStartOver: Bool = false
    @State private var appeared: Bool = false
    @State private var pulse: Bool = false

    public init(prompt: ResumePromptData, onResume: @escaping (URL, Double) -> Void, onStartOver: @escaping (URL) -> Void, onCancel: @escaping () -> Void) {
        self.prompt = prompt
        self.onResume = onResume
        self.onStartOver = onStartOver
        self.onCancel = onCancel
    }

    public var body: some View {
        ZStack {
            Color.black.opacity(0.35)
                .ignoresSafeArea()
                .onTapGesture { onCancel() }

            VStack {
                Spacer()

                VStack(spacing: 10) {
                    // Progress line
                    GeometryReader { geo in
                        ZStack(alignment: .leading) {
                            Capsule().fill(ShadcnTheme.border.opacity(0.4)).frame(height: 6)
                            Capsule().fill(ShadcnTheme.primary)
                                .frame(width: max(6, geo.size.width * CGFloat(min(max(prompt.progress, 0), 1))), height: 6)
                        }
                    }
                    .frame(height: 6)
                    .frame(width: 260)

                    // Label
                    Text("Resume from \(prompt.formattedTime) of \(prompt.formattedDuration)?")
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundStyle(ShadcnTheme.foreground)

                    // Buttons
                    HStack(spacing: 10) {
                        Button { onStartOver(prompt.url) } label: {
                            Text("Start Over")
                                .font(.system(size: 12, weight: .medium))
                                .foregroundStyle(ShadcnTheme.foreground)
                                .padding(.horizontal, 14)
                                .frame(height: 30)
                                .background(Capsule().fill(ShadcnTheme.cardHover))
                                .overlay(Capsule().strokeBorder(ShadcnTheme.border, lineWidth: 1))
                        }
                        .buttonStyle(.plain)
                        .pointingHand()

                        Button { onResume(prompt.url, prompt.resumeSeconds) } label: {
                            HStack(spacing: 5) {
                                Image(systemName: "play.fill").font(.system(size: 11, weight: .bold))
                                Text("Continue").font(.system(size: 12, weight: .semibold))
                            }
                            .foregroundStyle(ShadcnTheme.primaryForeground)
                            .padding(.horizontal, 16)
                            .frame(height: 30)
                            .background(Capsule().fill(isHoveringResume ? ShadcnTheme.primaryHighlight : ShadcnTheme.primary))
                            .shadow(color: ShadcnTheme.primary.opacity(pulse ? 0.6 : 0.3), radius: pulse ? 12 : 6, y: 3)
                        }
                        .buttonStyle(.plain)
                        .pointingHand()
                        .onHover { h in isHoveringResume = h }
                    }
                }
                .padding(.horizontal, 20)
                .padding(.vertical, 14)
                .background(
                    RoundedRectangle(cornerRadius: 20, style: .continuous)
                        .fill(.ultraThinMaterial)
                )
                .background(
                    RoundedRectangle(cornerRadius: 20, style: .continuous)
                        .fill(ShadcnTheme.card.opacity(0.85))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 20, style: .continuous)
                        .strokeBorder(ShadcnTheme.primary.opacity(0.4), lineWidth: 1.5)
                )
                .shadow(color: .black.opacity(0.4), radius: 24, y: 10)
                .scaleEffect(appeared ? 1 : 0.85)
                .opacity(appeared ? 1 : 0)
                .offset(y: appeared ? 0 : 20)
                .padding(.bottom, 24)
                .onAppear {
                    withAnimation(.spring(response: 0.4, dampingFraction: 0.76)) { appeared = true }
                    withAnimation(.easeInOut(duration: 1.2).repeatForever(autoreverses: true)) { pulse = true }
                }
            }
        }
    }
}
