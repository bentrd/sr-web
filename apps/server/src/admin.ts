import type { Server } from "bun";
import type { WsData } from "./rooms";

type SrServer = Server<WsData>;
import {
	adminListScores,
	adminListRgScores,
	adminUpdateScore,
	adminUpdateRgScore,
	adminDeleteScore,
	adminDeleteRgScore,
	adminInsertScore,
	adminInsertRgScore,
	getAllTimeLeaderboard,
	getRgAllTimeLeaderboard,
} from "./leaderboard";
import type { Leaderboard, RgLeaderboard } from "@sr-web/protocol";

type Table = "scores" | "rg_scores";

function json(body: unknown, status = 200): Response {
	return new Response(JSON.stringify(body), {
		status,
		headers: { "content-type": "application/json" },
	});
}

function todayDate(): string {
	return new Date().toISOString().slice(0, 10);
}

// Re-broadcast the public leaderboards so connected clients see admin
// edits immediately. Without this the live UI shows stale data until
// the next genuine submission triggers a republish.
function rebroadcast(server: SrServer, table: Table): void {
	if (table === "scores") {
		const lb = getAllTimeLeaderboard(10);
		server.publish(
			"leaderboard-speed",
			JSON.stringify({
				type: "leaderboard",
				date: todayDate(),
				entries: lb.map((e) => ({ rank: e.rank, name: e.name, maxSpeed: e.maxSpeed })),
			} satisfies Leaderboard),
		);
	} else {
		const lb = getRgAllTimeLeaderboard(10);
		server.publish(
			"leaderboard-rg",
			JSON.stringify({
				type: "rg_leaderboard",
				date: todayDate(),
				entries: lb.map((e) => ({ rank: e.rank, name: e.name, maxStreak: e.maxStreak })),
			} satisfies RgLeaderboard),
		);
	}
}

export async function handleAdminRequest(
	req: Request,
	url: URL,
	server: SrServer,
): Promise<Response | null> {
	if (!url.pathname.startsWith("/admin")) return null;

	const expected = process.env.ADMIN_TOKEN;
	if (!expected) {
		return new Response(
			"admin disabled — set ADMIN_TOKEN env var (e.g. `fly secrets set ADMIN_TOKEN=...`)",
			{ status: 503 },
		);
	}

	const provided = req.headers.get("x-admin-token") ?? url.searchParams.get("token");
	if (provided !== expected) {
		return new Response("unauthorized", { status: 401 });
	}

	if (url.pathname === "/admin" || url.pathname === "/admin/") {
		return new Response(ADMIN_HTML, {
			headers: { "content-type": "text/html; charset=utf-8" },
		});
	}

	const m = url.pathname.match(/^\/admin\/api\/(scores|rg_scores)(?:\/(\d+))?$/);
	if (!m) return new Response("not found", { status: 404 });

	const table = m[1] as Table;
	const id = m[2] ? Number(m[2]) : null;

	try {
		if (req.method === "GET" && id === null) {
			return json(table === "scores" ? adminListScores() : adminListRgScores());
		}

		if (req.method === "POST" && id === null) {
			const body = (await req.json()) as Record<string, unknown>;
			const date = String(body.date ?? todayDate()).slice(0, 32);
			const name = String(body.player_name ?? "").slice(0, 24);
			const ts = Number(body.timestamp ?? Date.now());
			if (!name) return json({ error: "player_name required" }, 400);
			if (table === "scores") {
				const speed = Number(body.max_speed);
				if (!Number.isFinite(speed)) return json({ error: "max_speed required" }, 400);
				const newId = adminInsertScore(date, name, speed, ts);
				rebroadcast(server, table);
				return json({ id: newId });
			} else {
				const streak = Number(body.max_streak);
				if (!Number.isFinite(streak)) return json({ error: "max_streak required" }, 400);
				const newId = adminInsertRgScore(date, name, streak, ts);
				rebroadcast(server, table);
				return json({ id: newId });
			}
		}

		if (req.method === "PATCH" && id !== null) {
			const body = (await req.json()) as Record<string, unknown>;
			const ok =
				table === "scores"
					? adminUpdateScore(id, body)
					: adminUpdateRgScore(id, body);
			if (!ok) return json({ error: "no rows updated" }, 404);
			rebroadcast(server, table);
			return json({ ok: true });
		}

		if (req.method === "DELETE" && id !== null) {
			const ok =
				table === "scores" ? adminDeleteScore(id) : adminDeleteRgScore(id);
			if (!ok) return json({ error: "not found" }, 404);
			rebroadcast(server, table);
			return json({ ok: true });
		}
	} catch (err) {
		return json({ error: err instanceof Error ? err.message : "unknown" }, 500);
	}

	return new Response("method not allowed", { status: 405 });
}

const ADMIN_HTML = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<title>SR-web · leaderboard admin</title>
<meta name="viewport" content="width=device-width, initial-scale=1" />
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 24px;
    background: #0e0e12; color: #e6e6ea;
    font: 14px/1.4 ui-monospace, "SF Mono", Menlo, monospace;
  }
  h1 { font-size: 16px; margin: 0 0 16px; font-weight: 600; }
  .tabs { display: flex; gap: 8px; margin-bottom: 16px; }
  .tabs button {
    background: #1c1c22; color: #aaa; border: 1px solid #2a2a32;
    padding: 6px 14px; border-radius: 6px; cursor: pointer; font: inherit;
  }
  .tabs button.active { background: #2d4a8a; color: #fff; border-color: #3a5fb0; }
  .toolbar { display: flex; gap: 8px; margin-bottom: 12px; align-items: center; }
  .toolbar input {
    background: #1c1c22; border: 1px solid #2a2a32; color: #e6e6ea;
    padding: 4px 8px; border-radius: 4px; font: inherit;
  }
  button.action {
    background: #1c1c22; color: #e6e6ea; border: 1px solid #2a2a32;
    padding: 4px 10px; border-radius: 4px; cursor: pointer; font: inherit;
  }
  button.action:hover { background: #25252d; }
  button.danger { color: #ff7676; border-color: #5a2828; }
  button.danger:hover { background: #2a1818; }
  button.primary { background: #2d4a8a; border-color: #3a5fb0; color: #fff; }
  button.primary:hover { background: #355aa8; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; }
  th, td { padding: 6px 10px; text-align: left; border-bottom: 1px solid #1f1f26; }
  th { color: #888; font-weight: 500; background: #15151b; position: sticky; top: 0; }
  td input {
    width: 100%; background: transparent; border: 1px solid transparent;
    color: inherit; font: inherit; padding: 2px 4px; border-radius: 3px;
  }
  td input:focus { outline: none; border-color: #3a5fb0; background: #15151b; }
  .row-actions { white-space: nowrap; }
  .empty { color: #666; padding: 40px; text-align: center; }
  .ts { color: #888; font-size: 12px; }
  #status { margin-left: auto; color: #888; font-size: 12px; }
  #status.error { color: #ff7676; }
  #status.ok { color: #6fdc8c; }
</style>
</head>
<body>
<h1>SR-web · leaderboard admin</h1>

<div class="tabs">
  <button id="tab-scores" class="active">Speed scores</button>
  <button id="tab-rg">RG streaks</button>
</div>

<div class="toolbar">
  <button class="action primary" id="add">+ Add row</button>
  <button class="action" id="refresh">Refresh</button>
  <span id="status"></span>
</div>

<table id="grid">
  <thead></thead>
  <tbody></tbody>
</table>

<script>
(() => {
  const params = new URLSearchParams(location.search);
  const token = params.get('token');
  if (!token) {
    document.body.innerHTML = '<h1>missing ?token=...</h1>';
    return;
  }

  let table = 'scores';
  const cols = {
    scores: ['id', 'date', 'player_name', 'max_speed', 'timestamp'],
    rg_scores: ['id', 'date', 'player_name', 'max_streak', 'timestamp'],
  };
  const numericCols = new Set(['id', 'max_speed', 'max_streak', 'timestamp']);

  const statusEl = document.getElementById('status');
  function setStatus(msg, kind) {
    statusEl.textContent = msg || '';
    statusEl.className = kind || '';
    if (kind === 'ok') setTimeout(() => { if (statusEl.textContent === msg) statusEl.textContent = ''; }, 2000);
  }

  async function api(path, opts = {}) {
    const r = await fetch('/admin/api' + path, {
      ...opts,
      headers: { 'content-type': 'application/json', 'x-admin-token': token, ...(opts.headers || {}) },
    });
    if (!r.ok) {
      const t = await r.text();
      throw new Error(r.status + ': ' + t);
    }
    return r.json();
  }

  function fmtTs(ms) {
    if (!ms) return '';
    const d = new Date(ms);
    return d.toISOString().replace('T', ' ').slice(0, 19);
  }

  async function load() {
    setStatus('loading…');
    try {
      const rows = await api('/' + table);
      render(rows);
      setStatus(rows.length + ' rows', 'ok');
    } catch (e) {
      setStatus(e.message, 'error');
    }
  }

  function render(rows) {
    const thead = document.querySelector('#grid thead');
    const tbody = document.querySelector('#grid tbody');
    const headers = cols[table];
    thead.innerHTML = '<tr>' + headers.map(c => '<th>' + c + '</th>').join('') + '<th></th></tr>';
    if (rows.length === 0) {
      tbody.innerHTML = '<tr><td colspan="' + (headers.length + 1) + '" class="empty">no rows</td></tr>';
      return;
    }
    tbody.innerHTML = rows.map(row => {
      const cells = headers.map(c => {
        if (c === 'id') return '<td>' + row.id + '</td>';
        if (c === 'timestamp') {
          return '<td><input data-field="timestamp" data-id="' + row.id + '" value="' + (row.timestamp ?? '') + '" /><div class="ts">' + fmtTs(row.timestamp) + '</div></td>';
        }
        const v = row[c] ?? '';
        return '<td><input data-field="' + c + '" data-id="' + row.id + '" value="' + String(v).replace(/"/g, '&quot;') + '" /></td>';
      }).join('');
      return '<tr data-id="' + row.id + '">' + cells +
        '<td class="row-actions">' +
          '<button class="action" data-save="' + row.id + '">Save</button> ' +
          '<button class="action danger" data-del="' + row.id + '">Delete</button>' +
        '</td></tr>';
    }).join('');
  }

  function collectRow(id) {
    const inputs = document.querySelectorAll('input[data-id="' + id + '"]');
    const out = {};
    inputs.forEach(i => {
      const field = i.dataset.field;
      let v = i.value;
      if (numericCols.has(field)) v = Number(v);
      out[field] = v;
    });
    return out;
  }

  document.addEventListener('click', async (e) => {
    const t = e.target;
    if (!(t instanceof HTMLElement)) return;
    const saveId = t.dataset.save;
    const delId = t.dataset.del;
    if (saveId) {
      try {
        const body = collectRow(saveId);
        delete body.id;
        await api('/' + table + '/' + saveId, { method: 'PATCH', body: JSON.stringify(body) });
        setStatus('saved #' + saveId, 'ok');
      } catch (err) { setStatus(err.message, 'error'); }
    }
    if (delId) {
      if (!confirm('Delete row ' + delId + '?')) return;
      try {
        await api('/' + table + '/' + delId, { method: 'DELETE' });
        setStatus('deleted #' + delId, 'ok');
        await load();
      } catch (err) { setStatus(err.message, 'error'); }
    }
  });

  document.getElementById('refresh').addEventListener('click', load);

  document.getElementById('add').addEventListener('click', async () => {
    const name = prompt('Player name?');
    if (!name) return;
    const valueLabel = table === 'scores' ? 'max_speed' : 'max_streak';
    const valStr = prompt(valueLabel + '?');
    if (valStr === null) return;
    const val = Number(valStr);
    if (!Number.isFinite(val)) { setStatus('invalid number', 'error'); return; }
    const body = { player_name: name, date: new Date().toISOString().slice(0, 10), timestamp: Date.now() };
    body[valueLabel] = val;
    try {
      await api('/' + table, { method: 'POST', body: JSON.stringify(body) });
      setStatus('inserted', 'ok');
      await load();
    } catch (err) { setStatus(err.message, 'error'); }
  });

  document.getElementById('tab-scores').addEventListener('click', () => {
    table = 'scores';
    document.getElementById('tab-scores').classList.add('active');
    document.getElementById('tab-rg').classList.remove('active');
    load();
  });
  document.getElementById('tab-rg').addEventListener('click', () => {
    table = 'rg_scores';
    document.getElementById('tab-rg').classList.add('active');
    document.getElementById('tab-scores').classList.remove('active');
    load();
  });

  load();
})();
</script>
</body>
</html>`;
