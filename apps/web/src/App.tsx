import { BrowserRouter, Navigate, Route, Routes } from "react-router-dom";
import { AppProvider } from "./state/AppState";
import { Home } from "./lobby/Home";
import { Room } from "./lobby/Room";

export function App(): JSX.Element {
	return (
		<AppProvider>
			<BrowserRouter>
				<Routes>
					<Route path="/" element={<Home />} />
					<Route path="/r/:code" element={<Room />} />
					<Route path="*" element={<Navigate to="/" replace />} />
				</Routes>
			</BrowserRouter>
		</AppProvider>
	);
}
