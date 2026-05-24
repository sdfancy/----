const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
  "sidebarBg": "#11161d",
  "surfaceBg": "#181c24",
  "raisedBg": "#1f2430",
  "accent": "#0891b2",
  "density": 1
}/*EDITMODE-END*/;

/* ── 导航结构 ── */
const NAV_GROUPS = [
  {
    label: '监控',
    items: [
      { id: 'overview', label: '系统总览', icon: 'menu_overview' },
      { id: 'queue', label: '队列管理', icon: 'menu_queue' },
    ],
  },
  {
    label: '设备',
    items: [
      { id: 'robot', label: '机械臂控制', icon: 'robot' },
      { id: 'plc', label: 'PLC 监控', icon: 'plc' },
      { id: 'camera', label: '相机状态', icon: 'camera' },
    ],
  },
  {
    label: '系统',
    items: [
      { id: 'diagnostics', label: '诊断日志', icon: 'menu_log' },
      { id: 'config', label: '系统配置', icon: 'menu_config' },
    ],
  },
];

/* ── 模拟实时数据 ── */
const MOCK_DATA = {
  sysStatus: 'running',
  uptime: '127h 34m',
  plcEnqueueConn: 1,
  plcDequeueConn: 2,
  camConnected: true,
  camMode: 'dual_camera_11_12',
  arm1Connected: true,
  arm2Connected: true,
  arm1TaskRunning: true,
  arm2TaskRunning: false,
  queueLength: 14,
  overflowDrops: 0,
  timeoutFills: 3,
  completedTasks: 1247,
  dequeuePtr1: 6,
  dequeuePtr2: 11,
  arm1State: '运行中 (轨迹 3/8)',
  arm2State: '待命中',
  arm1Speed: 85,
  arm2Speed: 0,
  plcRawCmds: [
    { time: '14:32:18', channel: '9999', hex: '01 03 00 64 00 01 C5 D5', desc: '读取工件计数寄存器 D100' },
    { time: '14:32:17', channel: '9090', hex: '01 10 00 C8 00 02 04 00 00 00 06', desc: '写入出队指针1=6' },
    { time: '14:32:17', channel: '9999', hex: '01 03 00 C8 00 01 04 66', desc: '读取出队指针1确认' },
    { time: '14:32:15', channel: '9999', hex: '01 03 00 6E 00 02 25 D1', desc: '读取入队计数' },
    { time: '14:32:12', channel: '9090', hex: '01 06 00 6E 00 0E A9 FA', desc: '写入入队计数=14' },
  ],
  queueRows: [
    { ptr: 0, count: 42, hasData: true, data: 'X=124.5 Y=89.2 Z=22.1 R=15', sent: 1, done: false, ts: '14:30' },
    { ptr: 1, count: 43, hasData: true, data: 'X=124.8 Y=89.0 Z=22.1 R=15', sent: 1, done: false, ts: '14:30' },
    { ptr: 2, count: 44, hasData: true, data: 'X=125.1 Y=88.8 Z=22.0 R=16', sent: 1, done: true, ts: '14:31' },
    { ptr: 3, count: 45, hasData: true, data: 'X=125.4 Y=88.5 Z=22.0 R=16', sent: 1, done: true, ts: '14:31' },
    { ptr: 4, count: 46, hasData: true, data: 'X=125.7 Y=88.3 Z=21.9 R=17', sent: 1, done: true, ts: '14:32' },
    { ptr: 5, count: 47, hasData: true, data: 'X=126.0 Y=88.0 Z=21.9 R=17', sent: 0, done: false, ts: '14:32' },
    { ptr: 6, count: 48, hasData: false, data: '-', sent: 0, done: false, ts: '-' },
    { ptr: 7, count: 49, hasData: false, data: '-', sent: 0, done: false, ts: '-' },
  ],
  buf1: [{ ptr: 6, count: 48, data: 'X=126.0 Y=88.0 Z=21.9 R=17', sent: 2, done: false, src: 'queue' }],
  buf2: [{ ptr: 11, count: 53, data: 'X=128.1 Y=86.2 Z=21.5 R=18', sent: 1, done: false, src: 'queue' }],
  logs: [
    { ts: '14:32:18.203', level: 'info', device: 'PLC', msg: '读取工件计数 D100 → OK (14)' },
    { ts: '14:32:17.891', level: 'info', device: 'PLC', msg: '写入出队指针 arm1=6 确认' },
    { ts: '14:32:15.442', level: 'warn', device: 'Camera', msg: '2D 相机帧超时 15ms，使用上一帧填充' },
    { ts: '14:32:12.117', level: 'info', device: 'Queue', msg: '机械臂1 发送 XN=1 至 DUCO task 42' },
    { ts: '14:31:58.003', level: 'info', device: 'DUCO', msg: 'Arm1 运动段 3/8 完成，进入段 4' },
    { ts: '14:31:45.229', level: 'err', device: 'Queue', msg: '机械臂2 超时未响应，强制填充默认值 D0' },
    { ts: '14:31:22.096', level: 'info', device: 'PLC', msg: '入队口连接建立 (192.168.1.10:9999)' },
  ],
  cycleState: [
    { count: 52, ptr: 14, type: '2D+3D', got12: true, sent3DEnd: true, enqueued: 'Arm1' },
    { count: 53, ptr: 15, type: '2D+3D', got12: true, sent3DEnd: false, enqueued: '-' },
  ],
};

/* ── SVG 图标组件 ── */
const Icon = ({ name, size = 20, color = 'currentColor' }) => {
  const paths = {
    menu_overview: 'M3 12h4l3-8 4 16 3-8h4',
    menu_queue: 'M8 4h8M8 10h12M8 16h8M4 4v16',
    robot: 'M12 2a2 2 0 0 1 2 2c0 .74-.4 1.39-1 1.73V7h3a2 2 0 0 1 2 2v3h1a1 1 0 0 1 0 2h-1v3a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2v-3H5a1 1 0 0 1 0-2h1V9a2 2 0 0 1 2-2h3V5.73c-.6-.34-1-.99-1-1.73a2 2 0 0 1 2-2zM8 18h8v-3H8v3zm0-5h8V9H8v4z',
    plc: 'M4 4h16a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zm0 2v12h16V6H4zm3 2h10v2H7V8zm0 4h6v2H7v-2zm0 4h8v2H7v-2z',
    camera: 'M15 10l4.5-4.5V18.5L15 14M4 6h4l2-2h4l2 2h4a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2z',
    menu_log: 'M4 4h16v4H4V4zm0 6h16v4H4v-4zm0 6h10v4H4v-4zM18 16v2h-2v2h2v2h2v-2h2v-2h-2v-2z',
    menu_config: 'M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6zm8-1.9c.06-.36.06-.73 0-1.1l1.86-1.4-1-1.73-2.15.84a5.5 5.5 0 0 0-.93-.55l-.34-2.26h-2l-.33 2.27c-.33.15-.65.33-.93.54L12.02 9l-1 1.73 1.86 1.4a4.05 4.05 0 0 0 0 1.1L11.02 14.7l1 1.73 2.15-.85c.28.22.6.4.93.55l.34 2.26h2l.33-2.27a5.5 5.5 0 0 0 .93-.54l2.16.84 1-1.73L20 13.1z',
    start: 'M8 5v14l11-7z',
    stop: 'M6 6h12v12H6z',
    pause: 'M6 4h4v16H6V4zm8 0h4v16h-4V4z',
    refresh: 'M17.65 6.35A7.96 7.96 0 0 0 12 4a8 8 0 1 0 0 16 7.96 7.96 0 0 0 5.66-2.35l-1.42-1.42A5.98 5.98 0 0 1 12 18a6 6 0 1 1 0-12 5.98 5.98 0 0 1 4.24 1.76L13 11h7V4l-2.35 2.35z',
    alert: 'M12 2L1 21h22L12 2zm0 3.99L19.53 19H4.47L12 5.99zM11 16h2v2h-2zm0-6h2v4h-2z',
    network: 'M1 9l2 2c3.97-3.97 10.03-3.97 14 0l2-2c-5-5-13-5-18 0zm8 8l2 2c1.66-1.66 3.9-2.58 6.4-2.58s4.77.93 6.4 2.58l2-2c-2.24-2.24-5.2-3.58-8.4-3.58S10.63 14.76 9 17zm3-3c.88.88 2.04 1.42 3.4 1.42s2.52-.54 3.4-1.42L12 14z',
    export: 'M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z',
  };
  const d = paths[name] || paths.menu_overview;
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke={color} strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d={d} />
    </svg>
  );
};

/* ── LED 状态灯 ── */
const StatusLED = ({ status, size = 10, label }) => {
  const colors = { ok: '#22c55e', warn: '#f59e0b', err: '#ef4444', off: '#4b5563' };
  const color = colors[status] || colors.off;
  return (
    <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 13 }}>
      <span style={{ width: size, height: size, borderRadius: '50%', background: color, boxShadow: `0 0 ${size}px ${color}80`, display: 'inline-block', flexShrink: 0 }} />
      {label && <span style={{ color: '#8892A0' }}>{label}</span>}
    </span>
  );
};

/* ── 状态徽章 ── */
const Badge = ({ children, kind = 'neutral' }) => {
  const bg = { ok: '#064e3b', warn: '#78350f', err: '#7f1d1d', neutral: '#1e293b', accent: '#164e63' }[kind];
  const fg = { ok: '#6ee7b7', warn: '#fcd34d', err: '#fca5a5', neutral: '#94a3b8', accent: '#67e8f9' }[kind];
  return <span style={{ padding: '2px 10px', borderRadius: 999, fontSize: 12, fontWeight: 600, background: bg, color: fg, whiteSpace: 'nowrap' }}>{children}</span>;
};

/* ── 共享阴影 ── */
const SHADOW_RAISED = '0 1px 2px rgba(0,0,0,0.5), 0 8px 24px rgba(0,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.06)';

function App() {
  const [active, setActive] = React.useState('overview');
  const [confirmAction, setConfirmAction] = React.useState(null);
  const d = MOCK_DATA;
  const gap = 14 * TWEAK_DEFAULTS.density;
  const accent = TWEAK_DEFAULTS.accent;

  const sysStatusKind =
    d.sysStatus === 'running' ? 'ok' : d.sysStatus === 'paused' ? 'warn' : d.sysStatus === 'alarm' ? 'err' : 'off';
  const sysStatusLabel = { running: '运行中', paused: '已暂停', stopped: '已停止', alarm: '告警' }[d.sysStatus];

  /* ── 命令确认弹窗 ── */
  const cmdDialog = confirmAction ? (
    <div role="dialog" aria-label="确认操作" onClick={() => setConfirmAction(null)} style={{ position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.65)', display: 'grid', placeItems: 'center', zIndex: 999 }}>
      <div onClick={e => e.stopPropagation()} style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 16, padding: 28, minWidth: 360, maxWidth: '90vw', border: '1px solid rgba(255,255,255,0.06)', boxShadow: '0 2px 4px rgba(0,0,0,0.55), 0 24px 48px rgba(0,0,0,0.55)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 16 }}>
          <Icon name="alert" size={24} color={confirmAction.danger ? '#ef4444' : '#f59e0b'} />
          <strong style={{ fontSize: 17 }}>{confirmAction.title}</strong>
        </div>
        <p style={{ color: '#8892A0', lineHeight: 1.7, marginBottom: 24 }}>{confirmAction.desc}</p>
        <div style={{ display: 'flex', gap: 10, justifyContent: 'flex-end' }}>
          <button onClick={() => setConfirmAction(null)} style={{ minHeight: 40, padding: '0 20px', borderRadius: 10, border: '1px solid rgba(255,255,255,0.12)', background: 'transparent', color: '#E8ECF1', cursor: 'pointer', font: 'inherit' }}>取消</button>
          <button onClick={() => { setConfirmAction(null); }} style={{ minHeight: 40, padding: '0 20px', borderRadius: 10, border: 0, background: confirmAction.danger ? '#ef4444' : accent, color: '#fff', fontWeight: 600, cursor: 'pointer', font: 'inherit' }}>确认执行</button>
        </div>
      </div>
    </div>
  ) : null;

  const pageTitle = NAV_GROUPS.flatMap(g => g.items).find(i => i.id === active)?.label || '系统总览';

  /* ═══════════════════════════════════════════════
     页面渲染
     ═══════════════════════════════════════════════ */
  const renderPage = () => {
    /* ── 系统总览 ── */
    if (active === 'overview') {
      return (
        <div style={{ display: 'grid', gap }}>
          {/* 状态条 */}
          <div style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 12, padding: '12px 20px', display: 'flex', alignItems: 'center', gap: 16, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
            <StatusLED status={sysStatusKind} size={12} />
            <span style={{ fontWeight: 700, fontSize: 15 }}>系统状态：{sysStatusLabel}</span>
            <span style={{ color: '#8892A0', fontSize: 13, marginLeft: 'auto' }}>运行时长 {d.uptime}</span>
            <Badge kind={d.sysStatus === 'running' ? 'ok' : d.sysStatus === 'alarm' ? 'err' : 'warn'}>{sysStatusLabel}</Badge>
          </div>
          {/* KPI 卡片 */}
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap }}>
            {[
              { label: '队列长度', value: d.queueLength, unit: '条', sub: `溢出 ${d.overflowDrops} 次`, kind: d.queueLength > 20 ? 'warn' : 'ok' },
              { label: '完成工件', value: d.completedTasks, unit: '件', sub: `超时填充 ${d.timeoutFills} 次`, kind: 'ok' },
              { label: '臂1出队指针', value: d.dequeuePtr1, unit: '', sub: `缓存 ${d.buf1.length} 条`, kind: 'ok' },
              { label: '臂2出队指针', value: d.dequeuePtr2, unit: '', sub: `缓存 ${d.buf2.length} 条`, kind: 'ok' },
            ].map(kpi => (
              <article key={kpi.label} style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: '18px 20px', border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
                <div style={{ color: '#8892A0', fontSize: 13 }}>{kpi.label}</div>
                <div style={{ fontSize: 34, fontWeight: 800, marginTop: 10, color: '#E8ECF1' }}>{kpi.value}<span style={{ fontSize: 16, fontWeight: 400, color: '#8892A0' }}>{kpi.unit}</span></div>
                <div style={{ color: kpi.kind === 'warn' ? '#fbbf24' : '#8892A0', fontSize: 12, marginTop: 6 }}>{kpi.sub}</div>
              </article>
            ))}
          </div>
          {/* 设备连接 + 流程信息 */}
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap }}>
            <article style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: 20, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
              <h2 style={{ margin: '0 0 14px', fontSize: 16, fontWeight: 700 }}>设备连接状态</h2>
              <div style={{ display: 'grid', gap: 10 }}>
                {[
                  { device: 'PLC 入队口 (:9999)', ok: d.plcEnqueueConn > 0, detail: `连接数 ${d.plcEnqueueConn}` },
                  { device: 'PLC 出队口 (:9090)', ok: d.plcDequeueConn > 0, detail: `连接数 ${d.plcDequeueConn}` },
                  { device: '2D/3D 相机', ok: d.camConnected, detail: d.camMode === 'dual_camera_11_12' ? '双相机模式 11/12' : 'Legacy 单相机' },
                  { device: '机械臂1 (DUCO)', ok: d.arm1Connected, detail: d.arm1Connected ? '已连接 · 运行中' : '未连接' },
                  { device: '机械臂2 (DUCO)', ok: d.arm2Connected, detail: d.arm2Connected ? '已连接 · 待命中' : '未连接' },
                ].map((dev, i) => (
                  <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '8px 12px', borderRadius: 8, background: 'rgba(255,255,255,0.02)' }}>
                    <StatusLED status={dev.ok ? 'ok' : 'err'} size={8} />
                    <span style={{ flex: 1, fontSize: 14 }}>{dev.device}</span>
                    <span style={{ color: '#8892A0', fontSize: 12 }}>{dev.detail}</span>
                  </div>
                ))}
              </div>
            </article>
            <article style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: 20, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
              <h2 style={{ margin: '0 0 14px', fontSize: 16, fontWeight: 700 }}>入队流程信息</h2>
              <div style={{ display: 'grid', gap: 10 }}>
                {[
                  { label: '流程模式', value: d.camMode === 'dual_camera_11_12' ? '双相机 11/12' : 'Legacy 单相机' },
                  { label: '机械臂1', value: d.arm1State },
                  { label: '机械臂2', value: d.arm2State },
                  { label: '臂1 当前速度', value: `${d.arm1Speed}%` },
                  { label: '臂2 当前速度', value: `${d.arm2Speed}%` },
                ].map((r, i) => (
                  <div key={i} style={{ display: 'flex', justifyContent: 'space-between', padding: '6px 0', borderBottom: i < 4 ? '1px solid rgba(255,255,255,0.04)' : 'none' }}>
                    <span style={{ color: '#8892A0', fontSize: 13 }}>{r.label}</span>
                    <span style={{ fontSize: 14, fontWeight: 600 }}>{r.value}</span>
                  </div>
                ))}
              </div>
            </article>
          </div>
          {/* 最近事件 */}
          <article style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: 20, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
              <h2 style={{ margin: 0, fontSize: 16, fontWeight: 700 }}>最近事件</h2>
              <button onClick={() => setActive('diagnostics')} style={{ border: 0, background: 'transparent', color: accent, cursor: 'pointer', fontSize: 13, font: 'inherit' }}>查看全部 →</button>
            </div>
            <div style={{ display: 'grid', gap: 4 }}>
              {d.logs.slice(0, 5).map((l, i) => (
                <div key={i} style={{ display: 'grid', gridTemplateColumns: '80px 60px 1fr', gap: 12, padding: '6px 10px', borderRadius: 6, fontSize: 13, alignItems: 'center' }}>
                  <span style={{ color: '#8892A0', fontFamily: 'ui-monospace, "Cascadia Code", monospace', fontSize: 12 }}>{l.ts.split('.')[0]}</span>
                  <Badge kind={l.level === 'err' ? 'err' : l.level === 'warn' ? 'warn' : 'ok'}>{l.level.toUpperCase()}</Badge>
                  <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{l.msg}</span>
                </div>
              ))}
            </div>
          </article>
        </div>
      );
    }

    /* ── 队列管理 ── */
    if (active === 'queue') {
      return (
        <div style={{ display: 'grid', gap }}>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap }}>
            {[
              { label: '臂1 出队指针', value: d.dequeuePtr1, color: '#22c55e' },
              { label: '臂2 出队指针', value: d.dequeuePtr2, color: '#67e8f9' },
              { label: '主队列长度', value: d.queueLength, color: '#fbbf24' },
              { label: '溢出覆盖', value: d.overflowDrops, color: d.overflowDrops > 0 ? '#ef4444' : '#22c55e' },
            ].map((p, i) => (
              <article key={i} style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: '16px 20px', border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
                <div style={{ color: '#8892A0', fontSize: 12 }}>{p.label}</div>
                <div style={{ fontSize: 36, fontWeight: 800, color: p.color, marginTop: 4 }}>{p.value}</div>
              </article>
            ))}
          </div>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap }}>
            {[
              { title: '机械臂1 队列', rows: d.queueRows, buf: d.buf1, accent: '#22c55e' },
              { title: '机械臂2 队列', rows: d.queueRows.map(r => ({ ...r, ptr: r.ptr + 6, count: r.count + 6 })), buf: d.buf2, accent: '#67e8f9' },
            ].map((arm, ai) => (
              <article key={ai} style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED, overflow: 'hidden' }}>
                <div style={{ padding: '14px 16px', borderBottom: '1px solid rgba(255,255,255,0.06)', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <h2 style={{ margin: 0, fontSize: 15, fontWeight: 700 }}>{arm.title}</h2>
                  <StatusLED status="ok" size={8} />
                </div>
                <div style={{ overflowX: 'auto' }}>
                  <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: 12 }}>
                    <thead>
                      <tr style={{ borderBottom: '1px solid rgba(255,255,255,0.06)' }}>
                        {['指针', '计数', '队列数据', '有数据', '发送', '完成', '更新时间'].map(h => <th key={h} style={{ padding: '8px 10px', textAlign: 'left', fontWeight: 600, color: '#8892A0', fontSize: 11, whiteSpace: 'nowrap' }}>{h}</th>)}
                      </tr>
                    </thead>
                    <tbody>
                      {arm.rows.map((r, i) => (
                        <tr key={i} style={{ borderBottom: '1px solid rgba(255,255,255,0.03)' }}>
                          <td style={{ padding: '7px 10px', fontFamily: 'ui-monospace, monospace' }}>{r.ptr}</td>
                          <td style={{ padding: '7px 10px' }}>{r.count}</td>
                          <td style={{ padding: '7px 10px', maxWidth: 160, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', color: r.hasData ? '#E8ECF1' : '#8892A0' }}>{r.data}</td>
                          <td style={{ padding: '7px 10px' }}>{r.hasData ? 'Y' : <span style={{ color: '#ef4444' }}>N</span>}</td>
                          <td style={{ padding: '7px 10px' }}>{r.sent}</td>
                          <td style={{ padding: '7px 10px' }}>{r.done ? <span style={{ color: '#22c55e' }}>✓</span> : '—'}</td>
                          <td style={{ padding: '7px 10px', color: '#8892A0', fontSize: 11 }}>{r.ts}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
                {arm.buf.length > 0 && (
                  <div style={{ borderTop: '1px solid rgba(255,255,255,0.06)', padding: '10px 16px' }}>
                    <div style={{ fontSize: 12, color: '#8892A0', marginBottom: 6 }}>发送缓存区</div>
                    {arm.buf.map((b, bi) => (
                      <div key={bi} style={{ display: 'flex', gap: 16, fontSize: 12, padding: '4px 0', alignItems: 'center' }}>
                        <span style={{ fontFamily: 'ui-monospace, monospace' }}>ptr={b.ptr}</span>
                        <span>count={b.count}</span>
                        <span style={{ color: '#8892A0', flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{b.data}</span>
                        <Badge kind="warn">发送 {b.sent}次</Badge>
                      </div>
                    ))}
                  </div>
                )}
              </article>
            ))}
          </div>
          {/* 3D 流程周期 */}
          <article style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED, overflow: 'hidden' }}>
            <div style={{ padding: '14px 16px', borderBottom: '1px solid rgba(255,255,255,0.06)' }}>
              <h2 style={{ margin: 0, fontSize: 15, fontWeight: 700 }}>入队周期状态（11/12 + 2D/3D）</h2>
            </div>
            <div style={{ overflowX: 'auto' }}>
              <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: 12 }}>
                <thead>
                  <tr style={{ borderBottom: '1px solid rgba(255,255,255,0.06)' }}>
                    {['计数', '入队指针', '工件类型', '已收12', '已发3D结束', '已入队'].map(h => <th key={h} style={{ padding: '8px 10px', textAlign: 'left', fontWeight: 600, color: '#8892A0', fontSize: 11 }}>{h}</th>)}
                  </tr>
                </thead>
                <tbody>
                  {d.cycleState.map((c, i) => (
                    <tr key={i} style={{ borderBottom: '1px solid rgba(255,255,255,0.03)' }}>
                      <td style={{ padding: '7px 10px' }}>{c.count}</td>
                      <td style={{ padding: '7px 10px', fontFamily: 'ui-monospace, monospace' }}>{c.ptr}</td>
                      <td style={{ padding: '7px 10px' }}>{c.type}</td>
                      <td style={{ padding: '7px 10px' }}>{c.got12 ? <span style={{ color: '#22c55e' }}>✓</span> : '—'}</td>
                      <td style={{ padding: '7px 10px' }}>{c.sent3DEnd ? <span style={{ color: '#22c55e' }}>✓</span> : <span style={{ color: '#f59e0b' }}>待发</span>}</td>
                      <td style={{ padding: '7px 10px', fontWeight: 600 }}>{c.enqueued}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </article>
        </div>
      );
    }

    /* ── 机械臂控制 ── */
    if (active === 'robot') {
      return (
        <div style={{ display: 'grid', gap }}>
          {/* 命令栏 */}
          <div style={{ display: 'flex', gap: 10, flexWrap: 'wrap' }}>
            {[
              { icon: 'start', label: '启动', danger: false, title: '启动机械臂任务', desc: '将开始执行队列中的喷涂任务，请确认机械臂工作区域已清空。' },
              { icon: 'pause', label: '暂停', danger: false, title: '暂停机械臂', desc: '机械臂将完成当前运动段后暂停，可稍后恢复。' },
              { icon: 'stop', label: '停止', danger: true, title: '停止机械臂', desc: '立即停止所有机械臂运动，未完成任务将保留在队列中。' },
              { icon: 'refresh', label: '恢复', danger: false, title: '恢复任务', desc: '从暂停状态恢复，继续执行队列中的剩余任务。' },
              { icon: 'alert', label: '急停', danger: true, title: '⚠ 紧急停止', desc: '切断机械臂动力，所有任务立即终止。此操作不可自动恢复！' },
            ].map((cmd, i) => (
              <button key={i} type="button" onClick={() => setConfirmAction({ title: cmd.title, desc: cmd.desc, danger: cmd.danger })} style={{ minHeight: 52, padding: '0 22px', borderRadius: 12, border: cmd.danger ? '2px solid #ef4444' : '1px solid rgba(255,255,255,0.12)', background: cmd.danger ? 'rgba(239,68,68,0.1)' : TWEAK_DEFAULTS.raisedBg, color: cmd.danger ? '#fca5a5' : '#E8ECF1', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 8, font: 'inherit', fontWeight: 600, fontSize: 14, boxShadow: SHADOW_RAISED }}>
                <Icon name={cmd.icon} size={18} /><span>{cmd.label}</span>
              </button>
            ))}
          </div>
          {/* 双臂状态 */}
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap }}>
            {[
              { name: '机械臂 1 (DUCO)', connected: d.arm1Connected, state: d.arm1State, speed: d.arm1Speed, running: d.arm1TaskRunning, accent: '#22c55e', tasks: 1247, errors: 0 },
              { name: '机械臂 2 (DUCO)', connected: d.arm2Connected, state: d.arm2State, speed: d.arm2Speed, running: d.arm2TaskRunning, accent: '#67e8f9', tasks: 1189, errors: 2 },
            ].map((arm, ai) => (
              <article key={ai} style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: 24, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 20 }}>
                  <h2 style={{ margin: 0, fontSize: 18, fontWeight: 700 }}>{arm.name}</h2>
                  <Badge kind={arm.running ? 'ok' : 'neutral'}>{arm.running ? '运行中' : '待命中'}</Badge>
                </div>
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14 }}>
                  {[
                    { label: '连接状态', value: arm.connected ? '已连接' : '未连接', color: arm.connected ? '#22c55e' : '#ef4444' },
                    { label: '当前速度', value: `${arm.speed}%` },
                    { label: '运行状态', value: arm.state },
                    { label: '完成任务', value: `${arm.tasks} 件` },
                  ].map((r, i) => (
                    <div key={i}>
                      <div style={{ fontSize: 12, color: '#8892A0', marginBottom: 4 }}>{r.label}</div>
                      <div style={{ fontSize: 20, fontWeight: 700, color: r.color || '#E8ECF1' }}>{r.value}</div>
                    </div>
                  ))}
                </div>
                <div style={{ marginTop: 18 }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 12, color: '#8892A0', marginBottom: 6 }}>
                    <span>速度</span><span>{arm.speed}%</span>
                  </div>
                  <div style={{ height: 8, borderRadius: 999, background: 'rgba(255,255,255,0.06)', overflow: 'hidden' }}>
                    <div style={{ height: '100%', width: `${arm.speed}%`, borderRadius: 999, background: arm.accent, transition: 'width 0.5s' }} />
                  </div>
                </div>
              </article>
            ))}
          </div>
          {/* 运动段进度 */}
          <article style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: 20, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
            <h2 style={{ margin: '0 0 16px', fontSize: 16, fontWeight: 700 }}>机械臂1 运动段进度</h2>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(8, 1fr)', gap: 6 }}>
              {[1,2,3,4,5,6,7,8].map(seg => {
                const done = seg <= 3, activeSeg = seg === 4;
                return (
                  <div key={seg} style={{ textAlign: 'center' }}>
                    <div style={{ height: 8, borderRadius: 999, background: done ? '#22c55e' : activeSeg ? accent : 'rgba(255,255,255,0.06)', marginBottom: 4 }} />
                    <span style={{ fontSize: 11, color: activeSeg ? accent : done ? '#22c55e' : '#8892A0' }}>段{seg}</span>
                  </div>
                );
              })}
            </div>
            <div style={{ marginTop: 12, fontSize: 13, color: '#8892A0', display: 'flex', gap: 20 }}>
              <span>当前：轨迹 3/8</span><span>剩余时间：~42s</span>
            </div>
          </article>
        </div>
      );
    }

    /* ── PLC 监控 ── */
    if (active === 'plc') {
      return (
        <div style={{ display: 'grid', gap }}>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap }}>
            {[
              { label: '入队口 (:9999)', value: '已连接', ok: true, sub: `连接数 ${d.plcEnqueueConn}` },
              { label: '出队口 (:9090)', value: '已连接', ok: true, sub: `连接数 ${d.plcDequeueConn}` },
              { label: '通讯协议', value: 'Modbus TCP', ok: true, sub: '端口 9999/9090' },
              { label: 'Smart200 预留', value: '未启用', ok: false, sub: '默认禁用' },
            ].map((c, i) => (
              <article key={i} style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: '16px 20px', border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
                <div style={{ fontSize: 12, color: '#8892A0' }}>{c.label}</div>
                <div style={{ fontSize: 22, fontWeight: 700, color: c.ok ? '#22c55e' : '#8892A0', marginTop: 6 }}>{c.value}</div>
                <div style={{ fontSize: 12, color: '#8892A0', marginTop: 4 }}>{c.sub}</div>
              </article>
            ))}
          </div>
          <article style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED, overflow: 'hidden' }}>
            <div style={{ padding: '14px 16px', borderBottom: '1px solid rgba(255,255,255,0.06)', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
              <h2 style={{ margin: 0, fontSize: 15 }}>PLC 原始命令（最新在前）</h2>
              <span style={{ fontSize: 12, color: '#8892A0' }}>共 {d.plcRawCmds.length} 条</span>
            </div>
            <div style={{ overflowX: 'auto' }}>
              <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: 13 }}>
                <thead>
                  <tr style={{ borderBottom: '1px solid rgba(255,255,255,0.06)' }}>
                    {['时间', '通道', '原始 HEX', '解析说明'].map(h => <th key={h} style={{ padding: '8px 10px', textAlign: 'left', fontWeight: 600, color: '#8892A0', fontSize: 11 }}>{h}</th>)}
                  </tr>
                </thead>
                <tbody>
                  {d.plcRawCmds.map((cmd, i) => (
                    <tr key={i} style={{ borderBottom: '1px solid rgba(255,255,255,0.03)' }}>
                      <td style={{ padding: '8px 10px', fontFamily: 'ui-monospace, monospace', fontSize: 12 }}>{cmd.time}</td>
                      <td style={{ padding: '8px 10px' }}><Badge kind={cmd.channel === '9999' ? 'accent' : 'neutral'}>{cmd.channel}</Badge></td>
                      <td style={{ padding: '8px 10px', fontFamily: 'ui-monospace, monospace', fontSize: 11, color: '#a5b4fc' }}>{cmd.hex}</td>
                      <td style={{ padding: '8px 10px' }}>{cmd.desc}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </article>
        </div>
      );
    }

    /* ── 相机状态 ── */
    if (active === 'camera') {
      return (
        <div style={{ display: 'grid', gap }}>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap }}>
            {[
              { label: '相机模式', value: d.camMode === 'dual_camera_11_12' ? '双相机 11/12' : 'Legacy 单相机', ok: true },
              { label: '2D 相机', value: d.camConnected ? '已连接' : '未连接', ok: d.camConnected },
              { label: '3D 相机', value: d.camConnected ? '已连接' : '未连接', ok: d.camConnected },
            ].map((c, i) => (
              <article key={i} style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: '18px 20px', border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
                <div style={{ fontSize: 12, color: '#8892A0' }}>{c.label}</div>
                <div style={{ fontSize: 24, fontWeight: 700, color: c.ok ? '#22c55e' : '#ef4444', marginTop: 6 }}>{c.value}</div>
              </article>
            ))}
          </div>
          <article style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: 20, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
            <h2 style={{ margin: '0 0 16px', fontSize: 16, fontWeight: 700 }}>相机通道详情</h2>
            <div style={{ display: 'grid', gap: 10 }}>
              {[
                { ch: '11 (2D 特征)', status: 'ok', detail: '接收正常 · 最新帧 14:32:18' },
                { ch: '12 (3D 点云)', status: 'ok', detail: '接收正常 · 最新帧 14:32:17' },
                { ch: '2D 备用', status: 'ok', detail: '连接就绪' },
              ].map((ch, i) => (
                <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '10px 14px', borderRadius: 8, background: 'rgba(255,255,255,0.02)' }}>
                  <StatusLED status={ch.status} size={8} />
                  <span style={{ fontWeight: 600, fontSize: 14, minWidth: 100 }}>{ch.ch}</span>
                  <span style={{ color: '#8892A0', fontSize: 13 }}>{ch.detail}</span>
                </div>
              ))}
            </div>
          </article>
        </div>
      );
    }

    /* ── 诊断日志 ── */
    if (active === 'diagnostics') {
      return (
        <div style={{ display: 'grid', gap }}>
          <div style={{ display: 'flex', gap: 12, alignItems: 'center', flexWrap: 'wrap' }}>
            <span style={{ fontSize: 13, color: '#8892A0' }}>筛选：</span>
            {['全部', 'PLC', 'Queue', 'Camera', 'DUCO'].map(f => (
              <button key={f} style={{ padding: '6px 14px', borderRadius: 999, border: '1px solid rgba(255,255,255,0.1)', background: 'transparent', color: '#E8ECF1', cursor: 'pointer', fontSize: 12, font: 'inherit' }}>{f}</button>
            ))}
            {['INFO', 'WARN', 'ERR'].map(l => (
              <button key={l} style={{ padding: '6px 14px', borderRadius: 999, border: '1px solid rgba(255,255,255,0.1)', background: 'transparent', color: l === 'ERR' ? '#fca5a5' : l === 'WARN' ? '#fcd34d' : '#6ee7b7', cursor: 'pointer', fontSize: 12, font: 'inherit' }}>{l}</button>
            ))}
            <span style={{ marginLeft: 'auto', fontSize: 12, color: '#8892A0' }}>共 {d.logs.length} 条记录</span>
          </div>
          <article style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED, overflow: 'hidden' }}>
            <div style={{ overflowX: 'auto' }}>
              <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: 13 }}>
                <thead>
                  <tr style={{ borderBottom: '1px solid rgba(255,255,255,0.06)' }}>
                    {['时间', '级别', '设备', '消息'].map(h => <th key={h} style={{ padding: '8px 12px', textAlign: 'left', fontWeight: 600, color: '#8892A0', fontSize: 11 }}>{h}</th>)}
                  </tr>
                </thead>
                <tbody>
                  {d.logs.map((l, i) => (
                    <tr key={i} style={{ borderBottom: '1px solid rgba(255,255,255,0.03)', background: l.level === 'err' ? 'rgba(239,68,68,0.05)' : l.level === 'warn' ? 'rgba(245,158,11,0.04)' : 'transparent' }}>
                      <td style={{ padding: '8px 12px', fontFamily: 'ui-monospace, monospace', fontSize: 12, whiteSpace: 'nowrap' }}>{l.ts}</td>
                      <td style={{ padding: '8px 12px' }}><Badge kind={l.level === 'err' ? 'err' : l.level === 'warn' ? 'warn' : 'ok'}>{l.level.toUpperCase()}</Badge></td>
                      <td style={{ padding: '8px 12px', fontWeight: 600 }}>{l.device}</td>
                      <td style={{ padding: '8px 12px' }}>{l.msg}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </article>
        </div>
      );
    }

    /* ── 系统配置 ── */
    if (active === 'config') {
      return (
        <div style={{ display: 'grid', gap }}>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap }}>
            {[
              { title: '喷涂参数', rows: [
                { k: '喷涂间距 (mm)', v: '120' }, { k: '喷枪压力 (bar)', v: '3.5' },
                { k: '移动速度 (mm/s)', v: '250' }, { k: '喷幅宽度 (mm)', v: '60' },
                { k: '轨迹重复次数', v: '1' },
              ]},
              { title: '队列配置', rows: [
                { k: '主队列容量', v: '32' }, { k: '出队缓存大小', v: '4' },
                { k: '超时填充阈值 (ms)', v: '500' }, { k: '最大重发次数', v: '3' },
                { k: '入队模式', v: 'dual_camera_11_12' },
              ]},
            ].map((cfg, ci) => (
              <article key={ci} style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: 20, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
                <h2 style={{ margin: '0 0 14px', fontSize: 16, fontWeight: 700 }}>{cfg.title}</h2>
                <div style={{ display: 'grid', gap: 0 }}>
                  {cfg.rows.map((r, i) => (
                    <div key={i} style={{ display: 'flex', justifyContent: 'space-between', padding: '10px 0', borderBottom: i < cfg.rows.length - 1 ? '1px solid rgba(255,255,255,0.04)' : 'none', alignItems: 'center' }}>
                      <span style={{ color: '#8892A0', fontSize: 13 }}>{r.k}</span>
                      <span style={{ fontWeight: 600, fontSize: 14, fontFamily: 'ui-monospace, monospace' }}>{r.v}</span>
                    </div>
                  ))}
                </div>
              </article>
            ))}
          </div>
          <article style={{ background: TWEAK_DEFAULTS.raisedBg, borderRadius: 14, padding: 20, border: '1px solid rgba(255,255,255,0.06)', boxShadow: SHADOW_RAISED }}>
            <h2 style={{ margin: '0 0 14px', fontSize: 16, fontWeight: 700 }}>运动配方</h2>
            <div style={{ display: 'grid', gap: 6 }}>
              {[
                { name: '配方 A — 标准平面喷涂', joints: '6轴联动', points: 48, time: '~85s' },
                { name: '配方 B — 曲面工件', joints: '6轴联动', points: 72, time: '~130s' },
                { name: '配方 C — 快速点喷', joints: '4轴', points: 12, time: '~18s' },
              ].map((rcp, i) => (
                <div key={i} style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '12px 16px', borderRadius: 10, background: 'rgba(255,255,255,0.02)', border: '1px solid rgba(255,255,255,0.04)' }}>
                  <div>
                    <div style={{ fontWeight: 600, fontSize: 14 }}>{rcp.name}</div>
                    <div style={{ fontSize: 12, color: '#8892A0', marginTop: 2 }}>{rcp.joints} · {rcp.points} 轨迹点 · {rcp.time}</div>
                  </div>
                  <Badge kind="accent">已加载</Badge>
                </div>
              ))}
            </div>
          </article>
        </div>
      );
    }

    return null;
  };

  /* ═══════════════════════════════════════════════
     主布局
     ═══════════════════════════════════════════════ */
  return (
    <div style={{ minHeight: '100%', background: '#0f1115', color: '#E8ECF1', fontFamily: '"PingFang SC", "Noto Sans SC", "Source Han Sans CN", "Microsoft YaHei", system-ui, sans-serif', display: 'grid', gridTemplateColumns: '230px minmax(0, 1fr)' }}>
      {/* 侧栏 */}
      <aside style={{ background: TWEAK_DEFAULTS.sidebarBg, color: '#E8ECF1', padding: '18px 12px', display: 'flex', flexDirection: 'column', gap: 16, minHeight: '100vh', borderRight: '1px solid rgba(255,255,255,0.06)' }}>
        {/* 品牌 */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '0 8px' }}>
          <div style={{ width: 36, height: 36, borderRadius: 10, background: accent, display: 'grid', placeItems: 'center', fontWeight: 800, fontSize: 16 }}>SC</div>
          <div>
            <div style={{ fontWeight: 700, fontSize: 15 }}>喷涂控制</div>
            <div style={{ fontSize: 11, opacity: 0.5 }}>Spray Control HMI</div>
          </div>
        </div>

        {/* 导航 */}
        {NAV_GROUPS.map(group => (
          <nav key={group.label} aria-label={group.label}>
            <div style={{ fontSize: 10, textTransform: 'uppercase', letterSpacing: '0.14em', opacity: 0.4, margin: '0 0 6px 8px' }}>{group.label}</div>
            <div style={{ display: 'grid', gap: 2 }}>
              {group.items.map(item => {
                const sel = active === item.id;
                return (
                  <button key={item.id} type="button" onClick={() => setActive(item.id)} style={{ minHeight: 40, border: 0, borderRadius: 10, padding: '0 12px', color: '#E8ECF1', background: sel ? 'rgba(255,255,255,0.1)' : 'transparent', boxShadow: sel ? `inset 3px 0 0 ${accent}` : 'none', textAlign: 'left', cursor: 'pointer', font: 'inherit', fontSize: 14, display: 'flex', alignItems: 'center', gap: 10 }}>
                    <Icon name={item.icon} size={18} color={sel ? accent : 'rgba(255,255,255,0.5)'} />
                    <span>{item.label}</span>
                  </button>
                );
              })}
            </div>
          </nav>
        ))}

        {/* 底部状态 */}
        <div style={{ marginTop: 'auto', padding: '12px 10px', borderRadius: 10, background: 'rgba(255,255,255,0.04)', display: 'flex', flexDirection: 'column', gap: 8 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <StatusLED status={sysStatusKind} size={8} />
            <span style={{ fontSize: 13 }}>{sysStatusLabel}</span>
          </div>
          <div style={{ fontSize: 11, color: 'rgba(255,255,255,0.4)' }}>运行 {d.uptime}</div>
          <div style={{ fontSize: 11, color: 'rgba(255,255,255,0.4)' }}>完成 {d.completedTasks} 件</div>
        </div>
      </aside>

      {/* 主内容 */}
      <main style={{ padding: '20px 24px', minWidth: 0, display: 'grid', gridTemplateRows: 'auto 1fr', gap: 18 }}>
        <header style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 16 }}>
          <div>
            <div style={{ fontSize: 12, color: '#8892A0' }}>
              {NAV_GROUPS.find(g => g.items.some(i => i.id === active))?.label} / {pageTitle}
            </div>
            <h1 style={{ margin: '2px 0 0', fontSize: 26, fontWeight: 800 }}>{pageTitle}</h1>
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
            <span style={{ fontSize: 12, color: '#8892A0' }}>更新时间 14:32:18</span>
            <button type="button" style={{ minHeight: 38, padding: '0 16px', borderRadius: 10, border: '1px solid rgba(255,255,255,0.1)', background: TWEAK_DEFAULTS.raisedBg, color: '#E8ECF1', cursor: 'pointer', font: 'inherit', fontSize: 13, display: 'flex', alignItems: 'center', gap: 6, boxShadow: SHADOW_RAISED }}>
              <Icon name="refresh" size={14} /> 刷新
            </button>
            <button type="button" style={{ minHeight: 38, padding: '0 16px', borderRadius: 10, border: 0, background: accent, color: '#fff', cursor: 'pointer', font: 'inherit', fontSize: 13, fontWeight: 600, display: 'flex', alignItems: 'center', gap: 6 }}>
              <Icon name="export" size={14} color="#fff" /> 导出
            </button>
          </div>
        </header>

        <section style={{ minWidth: 0 }}>
          {renderPage()}
        </section>
      </main>

      {cmdDialog}
    </div>
  );
}

ReactDOM.createRoot(document.getElementById('root')).render(<App />);
