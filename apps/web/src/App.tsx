import { HashRouter, Navigate, Route, Routes } from "react-router-dom";
import { AppProvider } from "./state/AppState";
import { Home } from "./lobby/Home";
import { Room } from "./lobby/Room";
import { ChallengeJoin } from "./lobby/ChallengeJoin";

// HashRouter (not BrowserRouter) so the app works on GitHub Pages
// without a server-side rewrite. Routes encode after the # so static
// hosting can always serve index.html for any URL.
export function App(): JSX.Element {
	return (
		<AppProvider>
			<HashRouter>
				<Routes>
					<Route path="/" element={<Home />} />
					<Route path="/r/:code" element={<Room />} />
					<Route path="/challenge/speed" element={<ChallengeJoin code="SPEED" />} />
					<Route path="/challenge/rg" element={<ChallengeJoin code="RGCH1" />} />
					<Route path="/challenge/time" element={<ChallengeJoin code="RACE1" />} />
					<Route path="*" element={<Navigate to="/" replace />} />
				</Routes>
			</HashRouter>
		</AppProvider>
	);
}
