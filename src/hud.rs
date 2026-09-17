//! Native living-room HUD: flat wgpu tiles + bitmap labels (no webview, no CRT).

use std::sync::Arc;
use std::time::Instant;

use anyhow::{Context, Result};
use bytemuck::{Pod, Zeroable};
use wgpu::util::DeviceExt;
use winit::window::Window;

use crate::accounts;
use crate::catalog::Catalog;
use crate::skills::Service;
use crate::theme::Theme;

const QUAD_SHADER: &str = include_str!("shaders/quad.wgsl");

const FONT_COLS: u32 = 16;
const FONT_ROWS: u32 = 6;
const GLYPH: u32 = 8;

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct QuadUniforms {
    resolution: [f32; 2],
    time: f32,
    _pad: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct Vertex {
    pos: [f32; 2],
    uv: [f32; 2],
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct Instance {
    rect: [f32; 4],
    color: [f32; 4],
    extra: [f32; 4],
}

pub struct Gpu {
    surface: wgpu::Surface<'static>,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    quad_pipeline: wgpu::RenderPipeline,
    quad_bind: wgpu::BindGroup,
    quad_uniform: wgpu::Buffer,
    vertex_buf: wgpu::Buffer,
    index_buf: wgpu::Buffer,
    instance_buf: wgpu::Buffer,
    instance_cap: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HudScreen {
    Guide,
    Accounts,
}

pub struct Guide {
    pub screen: HudScreen,
    pub account_idx: usize,
    pub catalog: Catalog,
    pub row: usize,
    pub col: usize,
    pub status: String,
    pub chrome_line: String,
    pub ota_line: String,
    pub hidden: bool,
    pub command: Option<String>,
}

impl Guide {
    pub fn new(catalog: Catalog) -> Self {
        let screen = if accounts::needs_onboarding() {
            HudScreen::Accounts
        } else {
            HudScreen::Guide
        };
        Self {
            screen,
            account_idx: 0,
            catalog,
            row: 0,
            col: 0,
            status: "D-pad move · OK select · Back · Home guide · Play/Pause".into(),
            chrome_line: "Chrome · starting…".into(),
            ota_line: "OTA · off".into(),
            hidden: false,
            command: None,
        }
    }

    pub fn open_accounts(&mut self) {
        self.screen = HudScreen::Accounts;
        self.account_idx = 0;
        self.status = accounts::ONBOARDING_HEADLINE.into();
    }

    pub fn close_accounts(&mut self) {
        self.screen = HudScreen::Guide;
        self.go_home();
    }

    pub fn move_accounts(&mut self, dcol: isize) {
        let n = accounts::accounts_tile_count() as isize;
        self.account_idx = ((self.account_idx as isize + dcol).rem_euclid(n)) as usize;
    }

    pub fn focused_account(&self) -> Option<(usize, Service)> {
        if self.screen != HudScreen::Accounts {
            return None;
        }
        if self.account_idx >= accounts::ACCOUNTS_DONE_INDEX {
            return None;
        }
        accounts::login_service(self.account_idx).map(|s| (self.account_idx, s))
    }

    pub fn is_accounts_done(&self) -> bool {
        self.screen == HudScreen::Accounts && self.account_idx == accounts::ACCOUNTS_DONE_INDEX
    }

    pub fn move_by(&mut self, drow: isize, dcol: isize) {
        if self.catalog.rows.is_empty() {
            return;
        }
        let rows = self.catalog.rows.len() as isize;
        self.row = ((self.row as isize + drow).rem_euclid(rows)) as usize;
        let cols = self.catalog.rows[self.row].tiles.len() as isize;
        if cols == 0 {
            self.col = 0;
            return;
        }
        self.col = ((self.col as isize + dcol).rem_euclid(cols)) as usize;
    }

    pub fn go_home(&mut self) {
        self.screen = HudScreen::Guide;
        self.row = 0;
        self.col = 0;
        self.command = None;
        self.status = "home".into();
    }

    pub fn focused(&self) -> Option<&crate::catalog::Tile> {
        self.catalog.tile(self.row, self.col)
    }

    pub fn open_command(&mut self) {
        self.command = Some(String::new());
        self.status =
            "play <query> [on youtube|netflix|prime|disney]  /  pause fullscreen back home".into();
    }

    pub fn close_command(&mut self) {
        self.command = None;
    }
}

impl Gpu {
    pub async fn new(window: Arc<Window>) -> Result<Self> {
        let size = window.inner_size();
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            // GL keeps Linux boxes without a Vulkan ICD (and this sketch VM) alive.
            backends: wgpu::Backends::PRIMARY | wgpu::Backends::GL,
            flags: wgpu::InstanceFlags::default(),
            memory_budget_thresholds: Default::default(),
            backend_options: Default::default(),
            display: None,
        });

        let surface = instance.create_surface(window.clone())?;
        let adapter = match instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::LowPower,
                compatible_surface: Some(&surface),
                force_fallback_adapter: false,
                apply_limit_buckets: true,
            })
            .await
        {
            Ok(adapter) => adapter,
            Err(err) => {
                log::warn!("hardware adapter failed ({err}); trying fallback");
                instance
                    .request_adapter(&wgpu::RequestAdapterOptions {
                        power_preference: wgpu::PowerPreference::LowPower,
                        compatible_surface: Some(&surface),
                        force_fallback_adapter: true,
                        apply_limit_buckets: true,
                    })
                    .await
                    .context("no wgpu adapter")?
            }
        };

        log::info!("gpu adapter: {:?}", adapter.get_info());

        let (device, queue) = adapter
            .request_device(&wgpu::DeviceDescriptor {
                label: Some("zappe"),
                required_features: wgpu::Features::empty(),
                experimental_features: wgpu::ExperimentalFeatures::disabled(),
                required_limits: wgpu::Limits::downlevel_defaults()
                    .using_resolution(adapter.limits()),
                memory_hints: Default::default(),
                trace: wgpu::Trace::Off,
            })
            .await?;

        let caps = surface.get_capabilities(&adapter);
        let format = caps
            .formats
            .iter()
            .copied()
            .find(|f| f.is_srgb())
            .unwrap_or(caps.formats[0]);
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode: wgpu::PresentMode::AutoVsync,
            alpha_mode: caps.alpha_modes[0],
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
            color_space: wgpu::SurfaceColorSpace::Auto,
        };
        surface.configure(&device, &config);

        let vertices = [
            Vertex {
                pos: [0.0, 0.0],
                uv: [0.0, 0.0],
            },
            Vertex {
                pos: [1.0, 0.0],
                uv: [1.0, 0.0],
            },
            Vertex {
                pos: [1.0, 1.0],
                uv: [1.0, 1.0],
            },
            Vertex {
                pos: [0.0, 1.0],
                uv: [0.0, 1.0],
            },
        ];
        let indices: [u16; 6] = [0, 1, 2, 0, 2, 3];
        let vertex_buf = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("quad-verts"),
            contents: bytemuck::cast_slice(&vertices),
            usage: wgpu::BufferUsages::VERTEX,
        });
        let index_buf = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("quad-idx"),
            contents: bytemuck::cast_slice(&indices),
            usage: wgpu::BufferUsages::INDEX,
        });

        let instance_cap = 512u32;
        let instance_buf = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("instances"),
            size: (instance_cap as u64) * std::mem::size_of::<Instance>() as u64,
            usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        let font = build_font_atlas();
        let font_size = wgpu::Extent3d {
            width: FONT_COLS * GLYPH,
            height: FONT_ROWS * GLYPH,
            depth_or_array_layers: 1,
        };
        let font_tex = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("font"),
            size: font_size,
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::R8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture: &font_tex,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            &font,
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(FONT_COLS * GLYPH),
                rows_per_image: Some(FONT_ROWS * GLYPH),
            },
            font_size,
        );
        let font_view = font_tex.create_view(&wgpu::TextureViewDescriptor::default());
        let font_samp = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("font-samp"),
            mag_filter: wgpu::FilterMode::Nearest,
            min_filter: wgpu::FilterMode::Nearest,
            ..Default::default()
        });

        let quad_uniform = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("quad-uniform"),
            contents: bytemuck::bytes_of(&QuadUniforms {
                resolution: [config.width as f32, config.height as f32],
                time: 0.0,
                _pad: 0.0,
            }),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });
        let quad_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("quad-bgl"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
            ],
        });
        let quad_bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("quad-bg"),
            layout: &quad_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: quad_uniform.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&font_view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::Sampler(&font_samp),
                },
            ],
        });
        let quad_shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("quad"),
            source: wgpu::ShaderSource::Wgsl(QUAD_SHADER.into()),
        });
        let quad_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("quad-pipe"),
            layout: Some(
                &device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
                    label: Some("quad-pl"),
                    bind_group_layouts: &[Some(&quad_layout)],
                    immediate_size: 0,
                }),
            ),
            vertex: wgpu::VertexState {
                module: &quad_shader,
                entry_point: Some("vs_main"),
                compilation_options: Default::default(),
                buffers: &[
                    Some(wgpu::VertexBufferLayout {
                        array_stride: std::mem::size_of::<Vertex>() as u64,
                        step_mode: wgpu::VertexStepMode::Vertex,
                        attributes: &wgpu::vertex_attr_array![0 => Float32x2, 1 => Float32x2],
                    }),
                    Some(wgpu::VertexBufferLayout {
                        array_stride: std::mem::size_of::<Instance>() as u64,
                        step_mode: wgpu::VertexStepMode::Instance,
                        attributes: &wgpu::vertex_attr_array![
                            2 => Float32x4,
                            3 => Float32x4,
                            4 => Float32x4
                        ],
                    }),
                ],
            },
            fragment: Some(wgpu::FragmentState {
                module: &quad_shader,
                entry_point: Some("fs_main"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            primitive: wgpu::PrimitiveState::default(),
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview_mask: None,
            cache: None,
        });

        Ok(Self {
            surface,
            device,
            queue,
            config,
            quad_pipeline,
            quad_bind,
            quad_uniform,
            vertex_buf,
            index_buf,
            instance_buf,
            instance_cap,
        })
    }

    pub fn resize(&mut self, width: u32, height: u32) {
        if width == 0 || height == 0 {
            return;
        }
        self.config.width = width;
        self.config.height = height;
        self.surface.configure(&self.device, &self.config);
    }

    pub fn render(&mut self, guide: &Guide, started: Instant) -> Result<()> {
        let frame = match self.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(frame)
            | wgpu::CurrentSurfaceTexture::Suboptimal(frame) => frame,
            wgpu::CurrentSurfaceTexture::Outdated | wgpu::CurrentSurfaceTexture::Lost => {
                self.surface.configure(&self.device, &self.config);
                return Ok(());
            }
            wgpu::CurrentSurfaceTexture::Timeout
            | wgpu::CurrentSurfaceTexture::Occluded
            | wgpu::CurrentSurfaceTexture::Validation => return Ok(()),
        };
        let view = frame
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());

        let res = [self.config.width as f32, self.config.height as f32];
        let time = started.elapsed().as_secs_f32();
        self.queue.write_buffer(
            &self.quad_uniform,
            0,
            bytemuck::bytes_of(&QuadUniforms {
                resolution: res,
                time,
                _pad: 0.0,
            }),
        );

        let instances = layout_ui(guide, res);
        let count = instances.len().min(self.instance_cap as usize) as u32;
        if count > 0 {
            self.queue.write_buffer(
                &self.instance_buf,
                0,
                bytemuck::cast_slice(&instances[..count as usize]),
            );
        }

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("zappe"),
            });
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("hud"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color {
                            r: Theme::BG[0] as f64,
                            g: Theme::BG[1] as f64,
                            b: Theme::BG[2] as f64,
                            a: 1.0,
                        }),
                        store: wgpu::StoreOp::Store,
                    },
                    depth_slice: None,
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });
            if count > 0 {
                pass.set_pipeline(&self.quad_pipeline);
                pass.set_bind_group(0, &self.quad_bind, &[]);
                pass.set_vertex_buffer(0, self.vertex_buf.slice(..));
                pass.set_vertex_buffer(1, self.instance_buf.slice(..));
                pass.set_index_buffer(self.index_buf.slice(..), wgpu::IndexFormat::Uint16);
                pass.draw_indexed(0..6, 0, 0..count);
            }
        }
        self.queue.submit(Some(encoder.finish()));
        self.queue.present(frame);
        Ok(())
    }
}

fn layout_ui(guide: &Guide, res: [f32; 2]) -> Vec<Instance> {
    let mut out = Vec::new();
    push_header(&mut out, guide, res);

    if guide.command.is_some() {
        push_command_bar(&mut out, guide, res);
    } else {
        push_text(
            &mut out,
            48.0,
            res[1] - 48.0,
            2.2,
            &guide.status,
            Theme::TEXT_MUTED,
        );
    }

    match guide.screen {
        HudScreen::Guide => layout_guide_rows(&mut out, guide),
        HudScreen::Accounts => layout_accounts(&mut out, guide, res),
    }
    out
}

fn push_header(out: &mut Vec<Instance>, guide: &Guide, res: [f32; 2]) {
    out.push(Instance {
        rect: [40.0, 36.0, 6.0, 44.0],
        color: Theme::ACCENT_WARM,
        extra: [0.0, 4.0, 0.0, 0.0],
    });
    push_text(out, 56.0, 40.0, 3.6, "Zappe", Theme::TEXT);
    push_text(
        out,
        56.0,
        78.0,
        2.0,
        "living-room launcher",
        Theme::TEXT_MUTED,
    );
    push_text(
        out,
        res[0] - 380.0,
        44.0,
        2.0,
        &guide.chrome_line,
        Theme::TEXT_MUTED,
    );
    push_text(
        out,
        res[0] - 380.0,
        68.0,
        2.0,
        &guide.ota_line,
        Theme::TEXT_MUTED,
    );
}

fn push_command_bar(out: &mut Vec<Instance>, guide: &Guide, res: [f32; 2]) {
    let bar_y = res[1] - 88.0;
    out.push(Instance {
        rect: [32.0, bar_y - 10.0, res[0] - 64.0, 44.0],
        color: Theme::BAR,
        extra: [0.0, 8.0, 0.0, 0.0],
    });
    if let Some(cmd) = &guide.command {
        let prompt = format!("> {cmd}_");
        push_text(out, 48.0, bar_y, 2.4, &prompt, Theme::ACCENT);
    }
}

fn layout_accounts(out: &mut Vec<Instance>, guide: &Guide, res: [f32; 2]) {
    push_text(
        out,
        48.0,
        130.0,
        2.8,
        "ACCOUNTS",
        Theme::ROW_LABEL,
    );
    push_text(
        out,
        48.0,
        168.0,
        2.2,
        accounts::ONBOARDING_HEADLINE,
        Theme::TEXT,
    );
    push_text(
        out,
        48.0,
        200.0,
        2.0,
        accounts::ONBOARDING_BODY,
        Theme::TEXT_MUTED,
    );
    push_text(
        out,
        48.0,
        232.0,
        1.8,
        "Tip: install 1Password extension in this Chrome profile.",
        Theme::TEXT_MUTED,
    );

    let mut x = 48.0;
    let y = 280.0;
    let w = 220.0;
    let h = 100.0;
    let gap = 20.0;
    for idx in 0..accounts::accounts_tile_count() {
        let focused = guide.account_idx == idx;
        if focused {
            out.push(Instance {
                rect: [x - 10.0, y - 10.0, w + 20.0, h + 20.0],
                color: Theme::FOCUS_RING,
                extra: [2.0, 12.0, 10.0, 0.0],
            });
        }
        out.push(Instance {
            rect: [x, y, w, h],
            color: if focused {
                Theme::SURFACE_FOCUS
            } else {
                Theme::SURFACE
            },
            extra: [0.0, 10.0, 0.0, 0.0],
        });
        let label = accounts::accounts_label(idx);
        let service = accounts::login_service(idx);
        let sub = service.map(|s| s.label()).unwrap_or("GUIDE");
        push_text(
            out,
            x + 16.0,
            y + 28.0,
            2.0,
            sub,
            if focused {
                Theme::ACCENT
            } else {
                Theme::TEXT_MUTED
            },
        );
        push_text(out, x + 16.0, y + 58.0, 2.2, label, Theme::TEXT);
        x += w + gap;
        if x + w > res[0] - 48.0 {
            x = 48.0;
            // single row layout for TV — wrap if narrow
        }
    }
}

fn layout_guide_rows(out: &mut Vec<Instance>, guide: &Guide) {
    let mut y = 120.0;
    for (ri, row) in guide.catalog.rows.iter().enumerate() {
        push_text(out, 48.0, y, 2.4, &row.label, Theme::ROW_LABEL);
        y += 36.0;
        let mut x = 48.0;
        for (ci, tile) in row.tiles.iter().enumerate() {
            let focused = ri == guide.row && ci == guide.col;
            let w = 260.0;
            let h = 96.0;
            if focused {
                out.push(Instance {
                    rect: [x - 10.0, y - 10.0, w + 20.0, h + 20.0],
                    color: Theme::FOCUS_RING,
                    extra: [2.0, 12.0, 10.0, 0.0],
                });
            }
            out.push(Instance {
                rect: [x, y, w, h],
                color: if focused {
                    Theme::SURFACE_FOCUS
                } else {
                    Theme::SURFACE
                },
                extra: [0.0, 10.0, 0.0, 0.0],
            });
            push_text(
                out,
                x + 18.0,
                y + 26.0,
                2.0,
                tile.service.label(),
                if focused {
                    Theme::ACCENT
                } else {
                    Theme::TEXT_MUTED
                },
            );
            push_text(
                out,
                x + 18.0,
                y + 56.0,
                2.2,
                &tile.title,
                Theme::TEXT,
            );
            x += w + 20.0;
        }
        y += 120.0;
    }
}

fn push_text(out: &mut Vec<Instance>, mut x: f32, y: f32, scale: f32, text: &str, color: [f32; 4]) {
    let w = GLYPH as f32 * scale;
    let h = GLYPH as f32 * scale;
    for ch in text.chars() {
        let code = ch as u32;
        if !(32..128).contains(&code) {
            x += w;
            continue;
        }
        let idx = code - 32;
        let gx = (idx % FONT_COLS) as f32;
        let gy = (idx / FONT_COLS) as f32;
        out.push(Instance {
            rect: [x, y, w, h],
            color,
            extra: [1.0, gx, gy, 0.0],
        });
        x += w + scale * 0.4;
    }
}

/// Public-domain-style 8x8 ASCII 32..127 packed into a 16x6 atlas.
fn build_font_atlas() -> Vec<u8> {
    let width = (FONT_COLS * GLYPH) as usize;
    let height = (FONT_ROWS * GLYPH) as usize;
    let mut pixels = vec![0u8; width * height];
    for code in 32u8..=127 {
        let bits = glyph(code);
        let idx = (code - 32) as u32;
        let gx = (idx % FONT_COLS) * GLYPH;
        let gy = (idx / FONT_COLS) * GLYPH;
        for row in 0..8 {
            let byte = bits[row];
            for col in 0..8 {
                if byte & (0x80 >> col) != 0 {
                    let x = gx as usize + col;
                    let y = gy as usize + row;
                    pixels[y * width + x] = 255;
                }
            }
        }
    }
    pixels
}

fn glyph(code: u8) -> [u8; 8] {
    // Compact 8x8 set covering the HUD labels. Unknown glyphs become a thin box.
    match code {
        b' ' => [0; 8],
        b'!' => [0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00],
        b'&' => [0x38, 0x44, 0x28, 0x10, 0x28, 0x44, 0x3A, 0x00],
        b'(' => [0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00],
        b')' => [0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00],
        b'=' => [0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00],
        b'?' => [0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00],
        b'_' => [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E],
        b'+' => [0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00],
        b'-' => [0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00],
        b'\'' => [0x18, 0x18, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00],
        b',' => [0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30],
        b'.' => [0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00],
        b'/' => [0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00, 0x00],
        b'>' => [0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00],
        b':' => [0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00],
        b'0' => [0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00],
        b'1' => [0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00],
        b'2' => [0x3C, 0x66, 0x06, 0x1C, 0x30, 0x60, 0x7E, 0x00],
        b'3' => [0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00],
        b'4' => [0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00],
        b'5' => [0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00],
        b'6' => [0x3C, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x3C, 0x00],
        b'7' => [0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00],
        b'8' => [0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00],
        b'9' => [0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00],
        b'A' => [0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00],
        b'B' => [0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00],
        b'C' => [0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00],
        b'D' => [0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00],
        b'E' => [0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00],
        b'F' => [0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00],
        b'G' => [0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00],
        b'H' => [0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00],
        b'I' => [0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00],
        b'J' => [0x3E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00],
        b'K' => [0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00],
        b'L' => [0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00],
        b'M' => [0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00],
        b'N' => [0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00],
        b'O' => [0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00],
        b'P' => [0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00],
        b'Q' => [0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00],
        b'R' => [0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00],
        b'S' => [0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00],
        b'T' => [0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00],
        b'U' => [0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00],
        b'V' => [0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00],
        b'W' => [0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00],
        b'X' => [0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00],
        b'Y' => [0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00],
        b'Z' => [0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00],
        b'a'..=b'z' => glyph(code - 32),
        _ => [0x7E, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00],
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn font_atlas_is_expected_size() {
        let atlas = build_font_atlas();
        assert_eq!(
            atlas.len(),
            (FONT_COLS * GLYPH * FONT_ROWS * GLYPH) as usize
        );
        assert!(atlas.iter().any(|&p| p > 0));
    }

    #[test]
    fn guide_wraps_focus() {
        let mut guide = Guide::new(Catalog::placeholder());
        guide.move_by(-1, 0);
        assert_eq!(guide.row, guide.catalog.rows.len() - 1);
        guide.move_by(1, 0);
        assert_eq!(guide.row, 0);
        assert!(guide.focused().is_some());
        guide.move_by(1, 0);
        guide.go_home();
        assert_eq!((guide.row, guide.col), (0, 0));
    }
}
