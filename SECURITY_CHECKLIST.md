# Forj Security Improvement Checklist

Purpose: track concrete security hardening work for client and server.

How to use:
- Check items only after code is merged and verified.
- Add PR/commit references next to completed items.
- Keep this list current as new risks are identified.

## Milestones and Effort

Legend:
- Effort S: 0.5-1 day
- Effort M: 2-4 days
- Effort L: 1+ week or cross-team work

### M1 - Immediate Hardening (High Impact, Fast)

- [ ] Enforce TLS everywhere in production (HTTPS + WSS only). [Effort M]
- [ ] Move JWT secret to required environment variable (fail fast if missing/weak/default). [Effort S]
- [ ] Stop storing plaintext login passwords locally. [Effort M]
- [ ] Ensure no secrets are logged (tokens, passwords, webhook secrets, API keys). [Effort S]
- [ ] Add client-side scheme guard to reject insecure base URLs in production builds. [Effort S]
- [ ] Add client-side scheme guard to reject insecure WebSocket URLs in production builds. [Effort S]
- [ ] Rate-limit login, register, and token refresh endpoints. [Effort M]

### M2 - Abuse Resistance and Session Safety

- [ ] Reduce access token lifetime (short-lived access tokens). [Effort M]
- [ ] Implement refresh token flow with rotation and reuse detection. [Effort L]
- [ ] Add server-side session revocation list/state. [Effort M]
- [ ] Invalidate refresh tokens on password change, account disable, and suspicious activity. [Effort M]
- [ ] Add account lockout/backoff policy for repeated failed login attempts. [Effort S]
- [ ] Add strict request validation for all write endpoints. [Effort M]
- [ ] Ensure authorization checks exist on every object-level action. [Effort M]
- [ ] Validate WebSocket auth at connect and on privileged events. [Effort M]

### M3 - Long-Term Defense in Depth

- [ ] Add optional certificate pinning strategy (or documented trust model and rotation process). [Effort L]
- [ ] Store credentials/tokens using OS secure storage (Windows and Linux keychains). [Effort L]
- [ ] Disable core dumps/minidumps in production builds where feasible. [Effort M]
- [ ] Add server-side voice frame rate/size limits and abuse throttling. [Effort M]
- [ ] Verify webhook signatures, freshness, and replay protection on callback paths. [Effort M]
- [ ] Add dependency/secrets/security scanning in CI and pre-commit hooks. [Effort M]
- [ ] Schedule external penetration test before public production launch. [Effort L]

### Contributor Quick Picks

- [ ] New contributors: pick any M1 item with Effort S.
- [ ] Regular contributors: pick M1/M2 items with Effort M.
- [ ] Maintainers/security owners: pick M2/M3 items with Effort L.

## 0. Critical Blockers (Do First)

- [ ] Enforce TLS everywhere in production (HTTPS + WSS only).
- [ ] Remove hardcoded production endpoints from client binaries.
- [ ] Move JWT secret to required environment variable (fail fast if missing/weak/default).
- [ ] Stop storing plaintext login passwords locally.
- [ ] Ensure no secrets are logged (tokens, passwords, webhook secrets, API keys).

## 1. Transport Security

- [ ] Add client-side scheme guard to reject insecure base URLs in production builds.
- [ ] Add client-side scheme guard to reject insecure WebSocket URLs in production builds.
- [ ] Add HSTS and secure proxy configuration in deployment docs.
- [ ] Add optional certificate pinning strategy (or documented trust model and rotation process).
- [ ] Verify TLS version and cipher policy at reverse proxy/load balancer.

## 2. Auth and Session Security

- [ ] Reduce access token lifetime (short-lived access tokens).
- [ ] Implement refresh token flow with rotation and reuse detection.
- [ ] Add server-side session revocation list/state.
- [ ] Bind refresh/session tokens to device/session identifiers.
- [ ] Invalidate refresh tokens on password change, account disable, and suspicious activity.
- [ ] Rate-limit login, register, and token refresh endpoints.
- [ ] Add account lockout/backoff policy for repeated failed login attempts.

## 3. Client Secret Handling

- [ ] Store credentials/tokens using OS secure storage:
- [ ] Windows: DPAPI/Credential Manager.
- [ ] Linux: Secret Service (libsecret/desktop keyring).
- [ ] Keep access tokens in memory for minimum time required.
- [ ] Zero sensitive buffers after use where practical.
- [ ] Disable core dumps/minidumps in production builds where feasible.
- [ ] Add crash-report redaction for sensitive fields.

## 4. API and Input Hardening

- [ ] Add strict request validation for all write endpoints.
- [ ] Add payload size limits and sane defaults server-wide.
- [ ] Ensure authorization checks exist on every object-level action.
- [ ] Harden file upload validation (type, size, content sniffing, image parsing limits).
- [ ] Add anti-abuse controls for message flood/spam endpoints.
- [ ] Add origin/host validation and strict CORS policy for any web-exposed APIs.

## 5. Real-time and Voice Security

- [ ] Validate WebSocket auth at connect and on privileged events.
- [ ] Add server-side voice frame rate/size limits and abuse throttling.
- [ ] Add per-channel membership checks for all voice signaling/data paths.
- [ ] Ensure no unauthenticated event broadcast paths exist.

## 6. KYC and Webhook Security

- [ ] Verify webhook signatures on every callback path.
- [ ] Enforce timestamp freshness / replay protection on callbacks.
- [ ] Store only minimum required KYC metadata; avoid retaining raw sensitive documents.
- [ ] Encrypt sensitive KYC-related fields at rest where applicable.
- [ ] Add admin audit trail for KYC status changes.

## 7. Data and Database Security

- [ ] Ensure least-privilege DB credentials (separate dev/staging/prod users).
- [ ] Add migration checks for security-sensitive schema changes.
- [ ] Encrypt backups and verify backup restore procedures.
- [ ] Add retention/deletion policy for messages, attachments, and KYC artifacts.

## 8. Operational Security

- [ ] Add dependency scanning in CI (client and server dependencies).
- [ ] Add static analysis/lint security rules in CI.
- [ ] Add secrets scanning in CI and pre-commit hooks.
- [ ] Add signed release artifacts and checksum verification docs.
- [ ] Document incident response runbook (token rotation, forced logout, key rollover).

## 9. Verification and Testing

- [ ] Create security regression tests for auth, authorization, and token invalidation.
- [ ] Add automated tests for insecure URL rejection in client.
- [ ] Add fuzz/property tests for parser-heavy endpoints (JSON, uploads, websocket payloads).
- [ ] Run periodic threat modeling review and update this checklist.
- [ ] Schedule external penetration test before public production launch.

## 10. Documentation and Ownership

- [ ] Assign owner for each security area (client, auth, infra, KYC, voice).
- [ ] Define severity labels and triage SLA for reported vulnerabilities.
- [ ] Publish responsible disclosure policy.
- [ ] Maintain changelog section for security fixes.

---

## Suggested Tracking Fields (Optional)

Use this template when closing an item:

- Item:
- Status: Done
- PR/Commit:
- Validation evidence:
- Date:
- Owner:
