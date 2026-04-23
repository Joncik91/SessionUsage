import CodexBarCore
import Foundation

extension SessionUsageCLI {
    static func usageHelp(version: String) -> String {
        """
        SessionUsage CLI \(version)

        Usage:
          sessionusage-cli usage [--format text|json]
                                 [--json]
                                 [--json-only]
                                 [--json-output] [-v|--verbose]
                                 [--log-level <trace|verbose|debug|info|warning|error|critical>]
                                 [--provider \(ProviderHelp.list)]
                                 [--account <label>] [--account-index <index>] [--all-accounts]
                                 [--no-credits] [--no-color] [--pretty] [--status] [--source <auto|web|cli|oauth|api>]
                                 [--web-timeout <seconds>] [--web-debug-dump-html]
                                 [--antigravity-plan-debug] [--augment-debug]

        Description:
          Print usage from enabled providers as text (default) or JSON. Honors your in-app toggles.
          Output format: use --json (or --format json) for JSON on stdout; use --json-output for JSON logs on stderr.
          Source behavior is provider-specific:
          - Codex: OpenAI web dashboard (usage limits, credits remaining, code review remaining, usage breakdown).
            Auto falls back to Codex CLI only when cookies are missing.
          - Claude: claude.ai API.
            Auto falls back to Claude CLI only when cookies are missing.
          - Kilo: app.kilo.ai API.
            Auto falls back to Kilo CLI when API credentials are missing or unauthorized.
          Token accounts are loaded from ~/.sessionusage/config.json (falling back to ~/.codexbar/config.json).
          Use --account or --account-index to select a specific token account, or --all-accounts to fetch all.
          Account selection requires a single provider.

        Global flags:
          -h, --help      Show help
          -V, --version   Show version
          -v, --verbose   Enable verbose logging
          --no-color      Disable ANSI colors in text output
          --log-level <trace|verbose|debug|info|warning|error|critical>
          --json-output   Emit machine-readable logs (JSONL) to stderr

        Examples:
          sessionusage-cli usage
          sessionusage-cli usage --provider claude
          sessionusage-cli usage --provider gemini
          sessionusage-cli usage --format json --provider all --pretty
          sessionusage-cli usage --provider all --json
          sessionusage-cli usage --status
          sessionusage-cli usage --provider codex --source web --format json --pretty
        """
    }

    static func costHelp(version: String) -> String {
        """
        SessionUsage CLI \(version)

        Usage:
          sessionusage-cli cost [--format text|json]
                                [--json]
                                [--json-only]
                                [--json-output] [-v|--verbose]
                                [--log-level <trace|verbose|debug|info|warning|error|critical>]
                                [--provider \(ProviderHelp.list)]
                                [--no-color] [--pretty] [--refresh]

        Description:
          Print local token cost usage from Claude/Codex native logs plus supported pi sessions.
          This does not require web or CLI access and uses cached scan results unless --refresh is provided.

        Examples:
          sessionusage-cli cost
          sessionusage-cli cost --provider claude --format json --pretty
        """
    }

    static func configHelp(version: String) -> String {
        """
        SessionUsage CLI \(version)

        Usage:
          sessionusage-cli config validate [--format text|json]
                                           [--json]
                                           [--json-only]
                                           [--json-output] [-v|--verbose]
                                           [--log-level <trace|verbose|debug|info|warning|error|critical>]
                                           [--pretty]
          sessionusage-cli config dump [--format text|json]
                                       [--json]
                                       [--json-only]
                                       [--json-output] [-v|--verbose]
                                       [--log-level <trace|verbose|debug|info|warning|error|critical>]
                                       [--pretty]

        Description:
          Validate or print the SessionUsage config file (default: validate).

        Examples:
          sessionusage-cli config validate --format json --pretty
          sessionusage-cli config dump --pretty
        """
    }

    static func rootHelp(version: String) -> String {
        """
        SessionUsage CLI \(version)

        Usage:
          sessionusage-cli [--format text|json]
                           [--json]
                           [--json-only]
                           [--json-output] [-v|--verbose]
                           [--log-level <trace|verbose|debug|info|warning|error|critical>]
                           [--provider \(ProviderHelp.list)]
                           [--account <label>] [--account-index <index>] [--all-accounts]
                           [--no-credits] [--no-color] [--pretty] [--status] [--source <auto|web|cli|oauth|api>]
                           [--web-timeout <seconds>] [--web-debug-dump-html]
                           [--antigravity-plan-debug] [--augment-debug]
          sessionusage-cli cost [--format text|json]
                                [--json]
                                [--json-only]
                                [--json-output] [-v|--verbose]
                                [--log-level <trace|verbose|debug|info|warning|error|critical>]
                                [--provider \(ProviderHelp.list)] [--no-color] [--pretty] [--refresh]
          sessionusage-cli config <validate|dump> [--format text|json]
                                                  [--json]
                                                  [--json-only]
                                                  [--json-output] [-v|--verbose]
                                                  [--log-level <trace|verbose|debug|info|warning|error|critical>]
                                                  [--pretty]

        Global flags:
          -h, --help      Show help
          -V, --version   Show version
          -v, --verbose   Enable verbose logging
          --no-color      Disable ANSI colors in text output
          --log-level <trace|verbose|debug|info|warning|error|critical>
          --json-output   Emit machine-readable logs (JSONL) to stderr

        Examples:
          sessionusage-cli
          sessionusage-cli --format json --provider all --pretty
          sessionusage-cli --provider all --json
          sessionusage-cli --provider gemini
          sessionusage-cli cost --provider claude --format json --pretty
          sessionusage-cli config validate --format json --pretty
        """
    }
}
