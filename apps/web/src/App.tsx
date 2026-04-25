import { HashRouter, Navigate, Route, Routes } from "react-router-dom";
import { AppProvider } from "./state/AppState";
import { Home } from "./lobby/Home";
import { Room } from "./lobby/Room";

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
					<Route path="*" element={<Navigate to="/" replace />} />
				</Routes>
			</HashRouter>
		</AppProvider>
	);
}
