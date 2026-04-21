import Foundation
import Testing

struct ProviderIconPreferenceTests {
    @Test
    func primaryLinuxTrayProvidersPreferPNGLogos() throws {
        let source = try String(contentsOf: Self.mainCURL, encoding: .utf8)

        for expected in [
            #""Claude", "Cl", "ProviderIcon-claude.png""#,
            #""Codex", "Cx", "ProviderIcon-codex.png""#,
            #""Copilot", "GH", "ProviderIcon-copilot.png""#,
            #""Cursor", "Cu", "ProviderIcon-cursor.png""#,
            #""Factory", "Fa", "ProviderIcon-factory.png""#,
            #""Gemini", "Ge", "ProviderIcon-gemini.png""#,
            #""Z.ai", "Za", "ProviderIcon-zai.png""#,
        ] {
            #expect(source.contains(expected))
        }
    }

    @Test
    func trayAssetResolutionSearchesRepositoryRoot() throws {
        let source = try String(contentsOf: Self.mainCURL, encoding: .utf8)

        #expect(source.contains(#"gchar *repo_dir = g_path_get_dirname(project_dir);"#))
        #expect(source.contains(#"g_build_filename(repo_dir, "sessionusage-symbolic.svg", NULL)"#))
        #expect(source.contains(#"g_build_filename(repo_dir, "Assets", "ProviderIcons", NULL)"#))
    }

    @Test
    func trayOptimizesLogoFillAndCopilotUsageLabels() throws {
        let source = try String(contentsOf: Self.mainCURL, encoding: .utf8)

        #expect(source.contains(#"#define SESSIONUSAGE_MENU_ICON_SIZE 16"#))
        #expect(source.contains(#"#define SESSIONUSAGE_LOGO_ALPHA_TRIM_THRESHOLD 8"#))
        #expect(source.contains(#"create_provider_logo_badge_pixbuf(style, SESSIONUSAGE_MENU_ICON_SIZE)"#))
        #expect(source.contains(#"gdk_pixbuf_new_subpixbuf"#))
        #expect(source.contains(#"gdk_pixbuf_scale_simple"#))
        #expect(source.contains(#"create_logo_chip_pixbuf(style->red, style->green, style->blue, size)"#))
        #expect(source.contains(#"if (pixel[3] < SESSIONUSAGE_LOGO_ALPHA_TRIM_THRESHOLD) {"#))
        #expect(source.contains(#"return badge;"#))
        #expect(source.contains(#"if (g_strcmp0(provider_id, "copilot") == 0) {"#))
        #expect(source.contains(#"return slot_index == 0 ? "Premium" : "Chat";"#))
    }

    @Test
    func preferredPNGLogoAssetsExist() throws {
        let pngSignature: [UInt8] = [0x89, 0x50, 0x4E, 0x47]

        for filename in [
            "ProviderIcon-claude.png",
            "ProviderIcon-codex.png",
            "ProviderIcon-copilot.png",
            "ProviderIcon-cursor.png",
            "ProviderIcon-factory.png",
            "ProviderIcon-gemini.png",
            "ProviderIcon-zai.png",
        ] {
            let assetURL = Self.packageRootURL
                .appendingPathComponent("Assets", isDirectory: true)
                .appendingPathComponent("ProviderIcons", isDirectory: true)
                .appendingPathComponent(filename, isDirectory: false)
            #expect(FileManager.default.fileExists(atPath: assetURL.path))

            let data = try Data(contentsOf: assetURL)
            #expect(data.count > pngSignature.count)
            #expect(Array(data.prefix(pngSignature.count)) == pngSignature)
        }
    }

    @Test
    func claudeAndCopilotPNGAssetsUseHighResolutionRasterizations() throws {
        let claudeDimensions = try Self.pngDimensions(
            of: Self.packageRootURL
                .appendingPathComponent("Assets", isDirectory: true)
                .appendingPathComponent("ProviderIcons", isDirectory: true)
                .appendingPathComponent("ProviderIcon-claude.png", isDirectory: false))
        let copilotDimensions = try Self.pngDimensions(
            of: Self.packageRootURL
                .appendingPathComponent("Assets", isDirectory: true)
                .appendingPathComponent("ProviderIcons", isDirectory: true)
                .appendingPathComponent("ProviderIcon-copilot.png", isDirectory: false))

        #expect(claudeDimensions.width == 64)
        #expect(claudeDimensions.height == 64)
        #expect(copilotDimensions.width == 64)
        #expect(copilotDimensions.height == 64)
    }

    private static let packageRootURL = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()

    private static let mainCURL = packageRootURL
        .appendingPathComponent("Sources", isDirectory: true)
        .appendingPathComponent("SessionUsage", isDirectory: true)
        .appendingPathComponent("main.c", isDirectory: false)

    private static func pngDimensions(of url: URL) throws -> (width: UInt32, height: UInt32) {
        let data = try Data(contentsOf: url)
        let signature = [UInt8](data.prefix(8))
        #expect(signature == [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
        let ihdrType = String(data: data.subdata(in: 12..<16), encoding: .ascii)
        #expect(ihdrType == "IHDR")
        let width = data.subdata(in: 16..<20).reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
        let height = data.subdata(in: 20..<24).reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
        return (width, height)
    }
}
