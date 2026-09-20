#!/usr/bin/env node
/* Phase5 批次16：ax_config.axs 的**无 GUI** 契约验收。
 *
 * 为什么必须有无 GUI 判据：
 *   container.put(key, w) 的 key 拼错**不会报任何错**，后端只是读不到这个字段
 *   ⇒ 症状是"UI 填了但没生效"，正是本项目反复归档的"静默走错"族（假绿形态）。
 *   本脚本把「key 与后端 TransportConfig 的 json tag 对齐」「开关语义」「类型匹配」
 *   三件事变成可执行的判据。
 *
 * 判据（A1–A7）：
 *   A1 执行不抛异常（create 模式 + edit 模式各跑一遍）
 *   A2 返回结构含 ui_panel/ui_container/ui_height/ui_width，且高度容得下 15 行布局
 *   A3 container 的每个键 ∈ 后端 json tag 白名单（★ 抄错键名的唯一防线）
 *   A4 无重复键（同名 put 会静默覆盖掉前一个字段）
 *   A5 每个键的控件类型与后端期望类型一致（spin⇒int / textmulti⇒string / combo⇒string）
 *   A6 开关：初始(tcp) ⇒ l2p 组禁用且禁用集合恰为"l2p 组 + 5 个设计上禁用的证书选择器"；
 *     切到 libp2p ⇒ 只剩那 5 个；再切回 tcp ⇒ 集合与初始逐元素相同（往返幂等）
 *   A7 编辑既有 libp2p listener（combo 被恢复到 libp2p）⇒ l2p 组必须是启用的
 *
 * 覆盖面（诚实声明）：本脚本**不**验证渲染效果/像素布局，也不启动 Qt 客户端；
 *   它验证的是"脚本可执行 + 字段契约 + 开关语义"。视觉验收仍属人工清单。
 *
 * 用法: node patches/axs_ui_check.js [ax_config.axs 路径]
 */
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const AXS = process.argv[2] ||
  path.join(__dirname, '..', 'ax_config.axs');
const OK = '[ok  ]', BAD = '[BAD ]', INFO = '[info]';
const fails = [];
function ck(cond, msg) {
  console.log(`${cond ? OK : BAD} ${msg}`);
  if (!cond) fails.push(msg);
  return !!cond;
}

/* ---- 后端 TransportConfig 的 json tag 白名单（实测自 pl_transport.go）----
 * 允许的容器键必须落在这里；多出来的键就是"UI 写了后端不认"的静默失效。 */
const GO_TAGS = new Set([
  'host_bind', 'port_bind', 'callback_addresses', 'encrypt_key', 'ssl',
  'ca_cert', 'server_cert', 'server_key', 'client_cert', 'client_key',
  'tcp_banner', 'error_answer', 'timeout', 'protocol',
  'l2p_peer_id', 'l2p_addrs', 'l2p_protocol', 'l2p_dial_timeout',
  'l2p_privkey', 'l2p_relays',
]);
// 键 ⇒ 后端期望的 JSON 类型（用于 A5 的控件类型匹配）
const GO_TYPE = {
  port_bind: 'int', timeout: 'int', l2p_dial_timeout: 'int', ssl: 'bool',
};
// l2p 组必须覆盖的键（批次4/5 的载荷侧设置）
const L2P_KEYS = ['l2p_protocol', 'l2p_privkey', 'l2p_relays', 'l2p_addrs', 'l2p_dial_timeout'];
// tcp 侧键：开关绝不能把它们一起禁掉
const TCP_KEYS = ['protocol', 'host_bind', 'port_bind', 'callback_addresses', 'timeout',
                  'tcp_banner', 'error_answer', 'encrypt_key', 'ssl'];

/* ---- 桩：Qt 控件 + form/ax 桥 ---- */
function makeHarness() {
  const widgets = [];           // 全部创建过的 widget（按创建顺序）
  const connects = [];          // form.connect 记录
  const layoutAdds = [];        // gridlayout.addWidget 记录
  const putLog = [];            // container.put 记录
  const container = new Map();
  let seq = 0, curCombo = null;

  function W(kind, extra) {
    const w = {
      __kind: kind, __seq: seq++, __tag: `${kind}#${seq - 1}`,
      __enabled: true, __setEnabled: 0,
      setEnabled(v) { this.__enabled = !!v; this.__setEnabled++; },
      getEnabled() { return this.__enabled; },
      setVisible() {}, getVisible() { return true; },
    };
    Object.assign(w, extra || {});
    widgets.push(w);
    return w;
  }
  const label = (t) => W('label', { setText() {}, text: () => t, __label: t });
  const textline = (t) => W('textline', { setText() {}, text: () => t || '', setPlaceholder() {}, setReadOnly() {} });
  const textmulti = (t) => W('textmulti', { setText() {}, text: () => t || '', appendText() {}, setPlaceholder() {}, setReadOnly() {} });
  const spin = () => W('spin', { setRange() {}, setValue(v) { this.__v = v; }, value: () => 0, setPlaceholder() {} });
  const combo = () => {
    const w = W('combo', {
      __items: [], __cur: -1, __text: '',
      addItem(t) { this.__items.push(t); if (this.__cur < 0) this.__cur = 0; },
      addItems() {}, setItems() {},
      clear() { this.__items = []; this.__cur = -1; },
      currentText() { return this.__cur >= 0 ? this.__items[this.__cur] : ''; },
      currentIndex() { return this.__cur; },
      setCurrentIndex(i) {
        this.__cur = i;
        // ★ 真实客户端 jsonUnmarshal 恢复配置时也会走这条 ⇒ 必须触发信号
        for (const c of connects) {
          if (c.w === this && c.sig === 'currentTextChanged') c.fn(this.currentText());
        }
      },
    });
    return w;
  };
  const check = () => W('check', { setChecked() {}, getChecked: () => false, setText() {} });
  const button = () => W('button', { setText() {} });
  const selector = () => W('selector_file', { setPlaceholder() {}, setText() {}, text: () => '' });
  const gridlayout = () => W('gridlayout', { addWidget(w, r, c, rs, cs) { layoutAdds.push({ w, r, c, rs, cs }); } });
  const vlayout = () => W('vlayout', { addWidget() {} });
  const panel = () => W('panel', { setLayout() {} });
  const dialog = () => W('dialog', { setLayout() {}, setSize() {}, exec() { return 0; } });
  const containerObj = {
    put(k, w) { putLog.push(k); container.set(k, w); },
  };

  const form = {
    create_label: label, create_textline: textline, create_textmulti: textmulti,
    create_spin: spin, create_combo: combo, create_check: check, create_button: button,
    create_selector_file: selector, create_gridlayout: gridlayout, create_vlayout: vlayout,
    create_panel: panel, create_dialog: dialog, create_container: () => containerObj,
    connect(w, sig, fn) { connects.push({ w, sig, fn }); return true; },
  };
  const ax = { interfaces: () => ['0.0.0.0', '127.0.0.1'], random_string: (n) => 'x'.repeat(Number(n) || 32) };
  return { form, ax, widgets, connects, layoutAdds, putLog, container };
}

function run(modeCreate) {
  const h = makeHarness();
  const sandbox = { console, Math, JSON, form: h.form, ax: h.ax };
  vm.createContext(sandbox);
  const src = fs.readFileSync(AXS, 'utf8');
  vm.runInContext(src, sandbox, { filename: AXS });
  const ret = vm.runInContext(`ListenerUI(${modeCreate ? 'true' : 'false'})`, sandbox);
  return { h, ret };
}

/* ---- A1：两种模式都能跑 ---- */
console.log(`${INFO} 被测 axs: ${AXS}`);
let create, edit;
try {
  create = run(true);
  ck(true, 'A1a create 模式(mode_create=true) 执行无异常');
} catch (e) {
  ck(false, `A1a create 模式抛异常: ${e.message}`);
  console.log(`\nRESULT: FAIL（1 项: 执行异常，后续判据不可判）`);
  process.exit(1);
}
try {
  edit = run(false);
  ck(true, 'A1b edit 模式(mode_create=false) 执行无异常');
} catch (e) {
  ck(false, `A1b edit 模式抛异常: ${e.message}`);
}

const { h, ret } = create;
/* ---- A2：返回结构 ---- */
ck(ret && ret.ui_panel && ret.ui_container, 'A2a 返回含 ui_panel/ui_container');
const rowMax = h.layoutAdds.reduce((m, a) => Math.max(m, a.r), 0);
ck(typeof ret.ui_height === 'number' && ret.ui_height >= rowMax * 45,
   `A2b ui_height=${ret.ui_height} 容得下 ${rowMax + 1} 行布局`);

/* ---- A3：键 ∈ 后端 tag 白名单 ---- */
const unknown = h.putLog.filter((k) => !GO_TAGS.has(k));
ck(unknown.length === 0, `A3 container 键全部命中后端 json tag（未命中: ${JSON.stringify(unknown)}）`);

/* ---- A4：无重复键 ---- */
const dup = h.putLog.filter((k, i) => h.putLog.indexOf(k) !== i);
ck(dup.length === 0, `A4 无重复 container 键（重复: ${JSON.stringify(dup)}）`);

/* ---- A5：控件类型与后端期望类型一致 ---- */
const typeBad = [];
for (const [k, w] of h.container) {
  const want = GO_TYPE[k];
  const got = w.__kind === 'spin' ? 'int'
    : w.__kind === 'check' ? 'bool'
    : (w.__kind === 'combo' || w.__kind === 'textline' || w.__kind === 'textmulti') ? 'string'
    : w.__kind;
  if (want && want !== got) typeBad.push(`${k}: 后端要 ${want}，UI 给 ${got}`);
}
ck(typeBad.length === 0, `A5 键的控件类型与后端期望一致（不符: ${JSON.stringify(typeBad)}）`);
const missingL2p = L2P_KEYS.filter((k) => !h.container.has(k));
ck(missingL2p.length === 0, `A5b 载荷侧 libp2p 键齐备（缺: ${JSON.stringify(missingL2p)}）`);

/* ---- A6：开关语义（tcp ⇒ l2p 组禁用，且不误伤 tcp 字段）---- */
const sig = h.connects.find((c) => c.sig === 'currentTextChanged' && c.w === h.container.get('protocol'));
ck(!!sig, 'A6a protocol 下拉已接到 currentTextChanged（开关有触发源）');
if (sig) {
  // ★ 与**基线**比集合，不比绝对条数：mTLS 的 5 个证书选择器是"设计上就禁用"的
  //   （勾了 Use mTLS 才启用）——把绝对条数写死会把它们算成开关的副作用 ⇒ 假红。
  //   基线还包含 l2p 组本身：create 模式 protocol 默认 tcp ⇒ 构造时就该是禁用的。
  const desc = (arr) => arr.map((w) => (w.__label ? `label:${w.__label}` : `${w.__kind}#${w.__seq}`)).join(' | ');
  const disabledSet = () => new Set(h.widgets.filter((w) => !w.getEnabled()));
  const l2pWidgets = new Set(L2P_KEYS.map((k) => h.container.get(k)));
  const baseline = disabledSet();
  const designOff = [...baseline].filter((w) => !l2pWidgets.has(w));
  const l2pOff0 = [...baseline].filter((w) => l2pWidgets.has(w));
  console.log(`${INFO} 初始禁用 ${baseline.size} 个 = l2p 组 ${l2pOff0.length} + 设计上禁用 ${designOff.length}`);
  ck(l2pOff0.length === L2P_KEYS.length, `A6b 初始态(protocol=tcp) ⇒ l2p 组 5 个输入控件全部禁用`);
  // 初始禁用集合必须**恰好** = 10 个 libp2p 控件(5 标签+5 输入) + 5 个设计上禁用的证书选择器，
  // 一个 tcp 侧控件都不许被牵连（这条同时覆盖了"没误伤 tcp 字段"）。
  ck(baseline.size === L2P_KEYS.length * 2 + 5 &&
     designOff.every((w) => w.__kind === 'selector_file' ||
                            (w.__kind === 'label' && /^libp2p/.test(w.__label || ''))),
     `A6c 初始禁用集合 = 10 个 libp2p 控件 + 5 个 mTLS 证书选择器（实测 ${baseline.size}, 非 l2p 输入者 ${designOff.length}）`);

  sig.fn('libp2p');
  const afterOn = disabledSet();
  ck(afterOn.size === 5 && [...afterOn].every((w) => w.__kind === 'selector_file'),
     `A6d protocol=libp2p ⇒ l2p 组全部启用，只剩设计上禁用的 5 个（实测 ${afterOn.size}: ${desc([...afterOn])}）`);

  sig.fn('tcp');
  const back = disabledSet();
  const same = back.size === baseline.size && [...back].every((w) => baseline.has(w));
  ck(same, `A6e tcp→libp2p→tcp 往返回到初始集合（${back.size} vs ${baseline.size}）`);
  sig.fn('libp2p');   // 复位，供后续判据使用
}

/* ---- A7：编辑既有 libp2p listener ⇒ 组必须可用 ---- */
const combo = edit.h.container.get('protocol');
combo.setCurrentIndex(combo.__items.indexOf('libp2p'));   // 模拟客户端恢复已存配置
const l2pOffEdit = L2P_KEYS.filter((k) => !edit.h.container.get(k).getEnabled());
ck(l2pOffEdit.length === 0,
   `A7 编辑 libp2p listener 时 l2p 组可用（仍禁用: ${JSON.stringify(l2pOffEdit)}）`);

console.log(`\nRESULT: ${fails.length ? `${fails.length} 项失败: ${JSON.stringify(fails)}` : '全部判据绿（A1–A7）'}`);
process.exit(fails.length ? 1 : 0);
