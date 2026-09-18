export const COMPANION_HOST = "zappe-tv.local";

export type CompanionStatus =
  | "waiting"
  | "phone_open"
  | "signing_in"
  | "needs_factor"
  | "connected"
  | "error";

export type CompanionSource = {
  id: string;
  label: string;
  wired: boolean;
  connected: boolean;
  harvest_skill?: string | null;
};

export type CompanionSession = {
  code: string;
  display_code: string;
  status: CompanionStatus;
  message: string;
  public_url: string;
  claim_url: string;
  qr_svg: string;
  host: string;
  port: number;
  expires_in_secs: number;
  nest_hidden: boolean;
  accounts_connected: boolean;
  source: string;
  source_label: string;
  source_wired: boolean;
  harvest_skill?: string | null;
  sources: CompanionSource[];
};

export const GUIDE_SOURCES: CompanionSource[] = [
  { id: "netflix", label: "Netflix", wired: true, connected: false },
  { id: "prime", label: "Prime Video", wired: false, connected: false },
  { id: "disney", label: "Disney+", wired: false, connected: false },
  { id: "youtube", label: "YouTube", wired: false, connected: false },
];

export const EMPTY_SESSION: CompanionSession = {
  code: "",
  display_code: "····-····",
  status: "waiting",
  message: "Starting the phone companion…",
  public_url: `http://${COMPANION_HOST}`,
  claim_url: `http://${COMPANION_HOST}`,
  qr_svg: "",
  host: COMPANION_HOST,
  port: 80,
  expires_in_secs: 0,
  nest_hidden: true,
  accounts_connected: false,
  source: "netflix",
  source_label: "Netflix",
  source_wired: true,
  harvest_skill: "netflix.continue_watching.v1",
  sources: GUIDE_SOURCES,
};

export function isCompanionConnected(status: CompanionStatus | undefined) {
  return status === "connected";
}

export function companionStatusLabel(session: CompanionSession): string {
  const name = session.source_label || "account";
  switch (session.status) {
    case "connected":
      return `${name} connected`;
    case "signing_in":
      return `Signing in to ${name}`;
    case "phone_open":
      return "Phone connected";
    case "needs_factor":
      return "Enter the extra code on your phone";
    case "error":
      return "Try again from the phone";
    default:
      return `Waiting for ${name}`;
  }
}

export function sessionSources(session: CompanionSession): CompanionSource[] {
  return session.sources?.length ? session.sources : GUIDE_SOURCES;
}

export function qrSvgMarkup(svg: string): string {
  if (!svg.includes("<svg")) return "";
  return svg.replace(/<script[\s\S]*?>[\s\S]*?<\/script>/gi, "");
}
