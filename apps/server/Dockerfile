# Lean Bun image — Bun is the only runtime requirement for the server.
FROM oven/bun:1 AS deps
WORKDIR /app
COPY package.json bun.lock ./
COPY packages/protocol/package.json packages/protocol/
COPY apps/server/package.json apps/server/
RUN bun install --frozen-lockfile

FROM oven/bun:1 AS runner
WORKDIR /app
COPY --from=deps /app/node_modules ./node_modules
COPY --from=deps /app/apps/server/node_modules ./apps/server/node_modules
COPY packages/protocol ./packages/protocol
COPY apps/server ./apps/server

ENV PORT=4000
EXPOSE 4000

CMD ["bun", "apps/server/src/index.ts"]
