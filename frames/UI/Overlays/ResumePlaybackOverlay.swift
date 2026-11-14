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
        GeometryReader { geo in
            let resumeX = geo.size.width * CGFloat(min(max(prompt.progress, 0), 1))

            ZStack {
                Color.black.opacity(0.2)
                    .ignoresSafeArea()
                    .onTapGesture { onCancel() }

                // Resume marker on timeline
                Rectangle()
                    .fill(ShadcnTheme.primary)
                    .frame(width: 2, height: 16)
                    .position(x: resumeX, y: geo.size.height - 8)
                    .allowsHitTesting(false)

                // Pill badge anchored to resume position
                VStack(spacing: 0) {
                    // tail
                    Triangle()
                        .fill(ShadcnTheme.card.opacity(0.95))
                        .frame(width: 14, height: 8)

                    VStack(spacing: 6) {
                        Text("Resume from \(prompt.formattedTime)?")
                            .font(.system(size: 12, weight: .semibold))
                            .foregroundStyle(ShadcnTheme.foreground)

                        HStack(spacing: 6) {
                            Button { onStartOver(prompt.url) } label: {
                                Text("Start Over")
                                    .font(.system(size: 11, weight: .medium))
                                    .foregroundStyle(ShadcnTheme.foregroundSecondary)
                                    .padding(.horizontal, 10)
                                    .frame(height: 26)
                                    .background(Capsule().fill(ShadcnTheme.cardHover))
                                    .overlay(Capsule().strokeBorder(ShadcnTheme.border, lineWidth: 1))
                            }
                            .buttonStyle(.plain)
                            .pointingHand()

                            Button { onResume(prompt.url, prompt.resumeSeconds) } label: {
                                HStack(spacing: 4) {
                                    Image(systemName: "play.fill").font(.system(size: 10, weight: .bold))
                                    Text("Continue").font(.system(size: 11, weight: .semibold))
                                }
                                .foregroundStyle(ShadcnTheme.primaryForeground)
                                .padding(.horizontal, 12)
                                .frame(height: 26)
                                .background(Capsule().fill(isHoveringResume ? ShadcnTheme.primaryHighlight : ShadcnTheme.primary))
                                .shadow(color: ShadcnTheme.primary.opacity(pulse ? 0.55 : 0.25), radius: pulse ? 10 : 5, y: 3)
                            }
                            .buttonStyle(.plain)
                            .pointingHand()
                            .onHover { h in isHoveringResume = h }
                        }
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 10)
                    .background(
                        Capsule(style: .continuous)
                            .fill(.ultraThinMaterial)
                    )
                    .background(
                        Capsule(style: .continuous)
                            .fill(ShadcnTheme.card.opacity(0.9))
                    )
                    .overlay(
                        Capsule(style: .continuous)
                            .strokeBorder(ShadcnTheme.primary.opacity(0.5), lineWidth: 1.5)
                    )
                    .shadow(color: .black.opacity(0.4), radius: 16, y: 6)
                }
                .scaleEffect(appeared ? 1 : 0.8)
                .opacity(appeared ? 1 : 0)
                .offset(y: appeared ? 0 : 12)
                .position(x: max(120, min(geo.size.width - 120, resumeX)), y: geo.size.height - 88)
            }
            .onAppear {
                withAnimation(.spring(response: 0.4, dampingFraction: 0.76)) { appeared = true }
                withAnimation(.easeInOut(duration: 1.1).repeatForever(autoreverses: true)) { pulse = true }
            }
        }
    }
}

struct Triangle: Shape {
    func path(in rect: CGRect) -> Path {
        var p = Path()
        p.move(to: CGPoint(x: rect.midX, y: rect.maxY))
        p.addLine(to: CGPoint(x: rect.minX, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        p.closeSubpath()
        return p
    }
}
