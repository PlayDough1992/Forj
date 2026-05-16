# Features Side-by-Side: Forj vs Discord

This comparison is meant to be practical and honest.

It compares:
- Forj as implemented in this repository today
- Discord as generally known from current public product behavior

## Executive Summary

Forj already covers core community communication needs: servers, channels, DMs, roles, moderation controls, and voice.

Discord currently leads in ecosystem breadth, polish depth, and enterprise-scale feature maturity.

If your community prioritizes trust-first identity and open-source control with strong core chat functionality, Forj is a compelling alternative.

## Feature Comparison Matrix

| Feature Area | Forj (Current) | Discord (General/Public) | Current Advantage |
|---|---|---|---|
| Account system | Username/email auth, JWT sessions, token-version invalidation | Mature auth/session ecosystem | Discord |
| Identity verification | KYC-gated access model available | No mandatory KYC for standard consumer usage | Forj (for trust-gated communities) |
| Servers and channels | Create/join servers, text channels | Mature servers, channels, forum/media variants | Discord |
| Direct messages | Supported | Supported, mature UX/features | Discord |
| Presence | Online/offline status + updates | Rich status and mature presence model | Discord |
| Roles and permissions | Role model with member/admin/owner and role management | Extensive role and permission tooling | Discord |
| Moderation controls | Roles, bans, content safety filter and freeze controls | Extensive moderation ecosystem and tooling | Discord |
| Invites | Invite-based server join flow | Mature invite links and controls | Discord |
| Voice channels | Voice support present (Windows + Linux), ForjR in-house noise reduction filter, user device selection | Mature voice ecosystem with Krisp integration, reliability at scale | Forj (noise reduction), Discord (scale/maturity) |
| Bots and automation | Bot support present in platform model | Large mature bot/integration ecosystem | Discord |
| Webhooks | Webhook model and signed callback paths | Broad webhook ecosystem and platform integrations | Discord |
| Open-source transparency | Client/server code visible and patchable by contributors | Closed-source internals | Forj |
| Deployment model | Official VPS-hosted project deployment | Vendor-hosted at global scale | Depends on priorities |
| UI customization velocity | Contributor-driven roadmap and patch flow | Vendor roadmap-driven | Forj for contributor-driven teams |

## Where Forj Is Strong Right Now

- Trust-first access with KYC-gated community model
- Open-source auditability and direct contribution path
- **ForjR noise reduction filter** — in-house background noise suppression (no third-party dependency)
- **Linux device selection** — respects user-configured audio input/output devices
- Core communication stack already in place:
  - Servers
  - Channels
  - DMs
  - Roles
  - Moderation controls
  - Voice with user-controlled noise reduction toggle

## Where Discord Is Stronger Right Now

- Feature depth and UX polish across more workflows
- Large ecosystem of integrations and bots
- Operational maturity and reliability at very large scale
- Broader accessibility and onboarding convenience

## Who Should Choose Forj

Forj is a strong fit for communities that need:
- Verified-identity-first operations
- Open-source transparency
- Faster community-driven feature/security iteration
- A serious alternative with core collaboration features already available

## Who Should Choose Discord

Discord is a better fit if you need:
- Immediate access to broad ecosystem/network effects
- Maximum convenience with minimal setup or governance effort
- Mature, highly polished workflows across many edge cases

## Bottom Line

Forj is already a real platform for trusted communities, not just a prototype.

Discord remains broader and more mature in total feature surface today.

For teams that care most about identity assurance, transparency, and control over evolution, Forj offers a meaningful advantage.
