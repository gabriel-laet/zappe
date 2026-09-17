//! Zappe visual identity — calm living-room contrast, no retro CRT.

pub struct Theme;

impl Theme {
    pub const BG: [f32; 3] = [0.055, 0.062, 0.078];
    pub const SURFACE: [f32; 4] = [0.11, 0.12, 0.15, 0.94];
    pub const SURFACE_FOCUS: [f32; 4] = [0.15, 0.17, 0.22, 0.98];
    pub const ACCENT: [f32; 4] = [0.38, 0.72, 0.98, 1.0];
    pub const ACCENT_WARM: [f32; 4] = [0.98, 0.58, 0.22, 1.0];
    pub const TEXT: [f32; 4] = [0.92, 0.94, 0.97, 1.0];
    pub const TEXT_MUTED: [f32; 4] = [0.55, 0.58, 0.65, 0.95];
    pub const ROW_LABEL: [f32; 4] = [0.48, 0.62, 0.78, 1.0];
    pub const FOCUS_RING: [f32; 4] = [0.38, 0.72, 0.98, 1.0];
    pub const BAR: [f32; 4] = [0.08, 0.09, 0.12, 0.96];
}
