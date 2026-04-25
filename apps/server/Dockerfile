# Lean Bun image — Bun is the only runtime requirement for the server.
FROM oven/bun:1 AS deps
WORKDIR /app
COPY package.json bun.lock ./
COPY packages/protocol/package.json packages/protocol/
COPY apps/server/package.json apps/server/
# Don't use --frozen-lockfile: the container's Bun version may differ from
# the one that wrote bun.lock locally, and a regenerated lockfile is fine
# for a relay server with two deps.
RUN bun install

FROM oven/bun:1 AS runner
WORKDIR /app
COPY --from=deps /app/node_modules ./node_modules
COPY --from=deps /app/apps/server/node_modules ./apps/server/node_modules
COPY packages/protocol ./packages/protocol
COPY apps/server ./apps/server

ENV PORT=4000
EXPOSE 4000

CMD ["bun", "apps/server/src/index.ts"]
