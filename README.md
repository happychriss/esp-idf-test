# Claude Code devcontainer template

This repo is structured to work out-of-the-box with VS Code **Dev Containers** using the custom root [docker-compose.yml](docker-compose.yml) service `dev`.

## Repo structure

| Path | Audience | Purpose |
|------|----------|---------|
| `README.md` | Human | You are here — setup and usage guide |
| `CLAUDE.md` | Claude | Session bootstrap and runtime context |
| `requirements.md` | Claude | Functional specification — what is being built |
| `external-docs/` | Claude (read-only) | Raw vendor material: datasheets, schematics, wikis |
| `hardware/` | Claude | Confirmed working driver config, derived from external-docs + experiment |
| `skills/` | Claude | Reusable workflow instructions loaded each session |
| `src/` | Both | Firmware source projects |
| `.devcontainer/` | Human | VS Code devcontainer config and Dockerfile |
| `claude-config/` | Human | Seed template for Claude's config volume (first-start only) |

## What’s included

- **Devcontainer image**: [.devcontainer/Dockerfile](.devcontainer/Dockerfile)
	- Ubuntu base image, non-root `ubuntu` user
	- Node.js (LTS) + `@anthropic-ai/claude-code`
	- Common CLI tooling (git, gh, jq, fzf, zsh, iptables/ipset, etc.)
	- Optional firewall helper script (allowlist-based)
- **Devcontainer config**: [.devcontainer/devcontainer.json](.devcontainer/devcontainer.json)
	- Uses root compose file: `../docker-compose.yml`
	- Persists shell history in a named volume
- **Project Claude template config**: [claude-config/](claude-config/)
	- On first container start, if `/home/ubuntu/.claude` is empty, it is seeded from `/workspace/claude-config`.

## Serial device access

The compose service maps a host serial device into the container as `/dev/ttyESP`.

- Default: host `/dev/ttyACM0` → container `/dev/ttyESP`
- Override with env var: `SERIAL_DEVICE=/dev/ttyUSB0`

Inside the container, the startup entrypoint detects the device GID and adds `ubuntu` to the owning group so non-root access works reliably.

## Customizing the container / project name

The compose file uses `COMPOSE_PROJECT_NAME` (from a local `.env` file) to build the dev container name.

Setup when you copy this template:

1. Edit `.env` and set `COMPOSE_PROJECT_NAME` to something unique per copy (e.g. `my-new-project`)

Result:

- The dev container name becomes `${COMPOSE_PROJECT_NAME}-dev`
- The compose “project name” also becomes `COMPOSE_PROJECT_NAME`, so networks/volumes won’t collide across copies

## Claude config persistence

Each copied template gets an **isolated** Claude config directory inside the container at `/home/ubuntu/.claude`.

This is persisted via a **Docker named volume** (`claude-home`) which is automatically scoped by `COMPOSE_PROJECT_NAME`, so different projects do not share Claude memory/settings.

The repo’s [claude-config/](claude-config/) is used as a **seed template** (copied only when the target dir is empty).

Where to put Claude “memory / skills / MCP” files:

- Claude reads/writes under `/home/ubuntu/.claude` inside the container.
- To ship defaults with this template, put them into [claude-config/](claude-config/) using the same paths you want under `~/.claude/`.
- After the first start, changes persist in the project’s `claude-home` volume (so they stay isolated per copied template).

## GitHub access — deploy key setup

To allow Claude to push/pull from a single GitHub repo without touching your other repositories:

**Inside the container:**
```bash
ssh-keygen -t ed25519 -C "my-project deploy key" -f ~/.ssh/my-project -N ""
cat ~/.ssh/my-project.pub   # copy this
```

Add to `~/.ssh/config`:
```
Host github-my-project
    HostName github.com
    User git
    IdentityFile ~/.ssh/my-project
    IdentitiesOnly yes
```

**On GitHub** (browser): repo → Settings → Deploy keys → Add deploy key → paste public key → tick "Allow write access"

**Test and set remote:**
```bash
ssh -T github-my-project    # should greet you with the repo name
git remote add origin git@github-my-project:yourname/my-project.git
git push -u origin main
```

The key is scoped to one repo only — it cannot access any of your other GitHub repositories.

## Optional: network allowlist (firewall)

If you want to restrict outbound access to typical development endpoints (GitHub, npm registry, VS Code marketplace/update URLs, Anthropic API, etc.), run:

`sudo /usr/local/bin/init-firewall.sh`

This is optional and is not enabled automatically.

