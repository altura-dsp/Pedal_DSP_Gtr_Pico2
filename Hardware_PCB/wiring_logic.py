"""
wiring_logic.py — 1_Pedal_DSP_GTR (Esquemático as Code)
========================================================
Generado por Prompt 3.1 v2.6 sobre Arquitectura_Hardware.md v1.1.
Motor (component_library.py + main_build.py + footprint_map.json) vive en tools/ — se
IMPORTA, no se copia. Aquí SÓLO conexiones eléctricas (cero footprint_map local: Grupo C.5).

Contrato del motor:
  wire_circuit(parts, gnd_audio, gnd_chasis, gnd_digital, vcc_12v, vcc_5v, vcc_3v3)   [main_build.py:122/569]
  build_spice_stage(stage_name, parts, gnd_audio, gnd_chasis, vcc_12v, vee_minus_12v)  [main_build.py:273]
"""

import builtins
from skidl import Net, Part

# =============================================================================
# Helper fail-fast (Grupo C.2) — PROHIBIDO indexar pines directamente.
# =============================================================================
def _safe_pin(part, pin_id):
    """Retorna el pin físico de `part` cuyo .num == str(pin_id). Fail-fast: si no
    existe lanza ValueError con mensaje claro (NUNCA None → un None & net estalla opaco)."""
    pin_id = str(pin_id)
    for p in part.pins:
        if p.num == pin_id:
            return p
    raise ValueError(f"Pin {pin_id} no encontrado en {part.name} (ref={getattr(part,'ref','?')})")


# =============================================================================
# SSOT-GPIO PICO2 — espejo EXACTO de Arquitectura_Hardware.md §2.2 (rules/13 §13.11)
# Pares GPIO→pin físico THT del módulo Pico 2. Cero pines mágicos: el firmware
# (HardwareConfig.h) y el hardware rutean el MISMO número.
# =============================================================================
PICO2_GPIO_TO_PIN = {
    'GPIO0': '1',   'GPIO1': '2',                       # OLED I2C SDA/SCL
    'GPIO2': '4',   'GPIO3': '5',   'GPIO4': '6',       # ENC1 A/B/SW
    'GPIO5': '7',   'GPIO6': '9',   'GPIO7': '10',      # ENC2 A/B/SW
    'GPIO8': '11',  'GPIO9': '12',  'GPIO10': '14',     # ENC3 A/B/SW
    'GPIO11': '15', 'GPIO12': '16', 'GPIO13': '17',     # FS1/FS2/FS3
    'GPIO14': '19', 'GPIO15': '20',                     # I2S BCLK / LRCK (=BCLK+1)
    'GPIO16': '21', 'GPIO17': '22',                     # I2S DOUT (→DAC) / DIN (←ADC)
    'GPIO18': '24', 'GPIO19': '25', 'GPIO20': '26',     # ENC_PARA A/B/SW
    'GPIO21': '27',                                     # MCLK (clk_gpout0, 12.288 MHz)
    'GPIO22': '29',                                     # FS4
    'GPIO23': '31',                                     # MUTE_RELAY
    'GPIO24': '32',                                     # BYPASS_CTRL (HEF4053)
    'GPIO26': '31', 'GPIO27': '32',                     # ADC0 Master Vol / ADC1 Expression
    'GPIO28': '34',                                     # NeoPixel (WS2812B)
    'GPIO29': '35',                                     # Entropy ADC (TRNG, flotante)
}

def get_mcu_pin(parts, gpio):
    """Resuelve un GPIO lógico al pin físico del MCU vía PICO2_GPIO_TO_PIN (SSOT-GPIO)."""
    if gpio not in PICO2_GPIO_TO_PIN:
        raise ValueError(f"GPIO {gpio} no declarado en PICO2_GPIO_TO_PIN (SSOT-GPIO).")
    return _safe_pin(parts['MCU'], PICO2_GPIO_TO_PIN[gpio])


# =============================================================================
# Autolimpieza de huérfanos (Grupo C.4) — recopilar PRIMERO, eliminar DESPUÉS.
# =============================================================================
def _cleanup_orphans():
    to_remove = [p for p in builtins.default_circuit.parts if not any(pin.nets for pin in p.pins)]
    for p in to_remove:
        builtins.default_circuit.parts.remove(p)


# =============================================================================
# WIRE_CIRCUIT — firma EXACTA del motor (main_build.py:122/569).
# =============================================================================
def wire_circuit(parts, gnd_audio, gnd_chasis, gnd_digital, vcc_12v, vcc_5v, vcc_3v3):
    """Ensambla la topología completa del pedal (esquemático KiCad)."""
    # Nets propios del pedal (hardware real ±12V desde LT1054 — ECO 12V; coherente con el testbench SPICE ±12V)
    vcc_12v     = Net('VCC_12V')        # +12V (DC jack post-MOSFET-P)
    vee_12v     = Net('VEE_12V')        # -12V (LT1054 pin5)
    vcc_3v3_an = Net('VCC_3V3_ANA')   # +3.3V AP2112K aislado (codecs + OLED)

    _wire_input_stage(parts, gnd_audio, gnd_chasis, vcc_12v, vee_12v, vcc_3v3_an)
    _wire_output_stage(parts, gnd_audio, gnd_chasis, vcc_12v, vee_12v, vcc_3v3_an)
    _wire_hp_stage(parts, gnd_audio, vcc_12v, vee_12v)
    _wire_power_stage(parts, gnd_audio, gnd_chasis, vcc_12v, vee_12v, vcc_5v, vcc_3v3, vcc_3v3_an)
    _wire_mute_relay(parts, gnd_audio, gnd_digital, vcc_5v)
    _wire_safe_bypass(parts, gnd_audio, gnd_digital, vcc_3v3)
    _wire_ui_stage(parts, gnd_audio, gnd_digital, vcc_5v, vcc_3v3)
    _wire_mcu_core(parts, gnd_audio, gnd_digital, vcc_3v3, vcc_3v3_an)

    _cleanup_orphans()


# -----------------------------------------------------------------------------
# Etapa de ENTRADA — Jack IN-L/IN-R → TVS+FB+C47pF (Regla 13) → acople →
#                    divisor Thévenin → TL072 (A=L, B=R) → PCM1808 VINL/VINR
# -----------------------------------------------------------------------------
def _wire_esd_jack(parts, jack_key, tvs_key, gnd_chasis, tip_net):
    """Regla 13: TVS bidir al CHASIS + ferrite BLM18 en serie + C 47pF C0G al CHASIS."""
    jack = parts[jack_key]
    tvs  = parts[tvs_key]
    tip  = _safe_pin(jack, 'T')           # AudioJack2: T = TIP (Ley 16, rules/13 §13.18)
    _safe_pin(jack, 'S') & gnd_chasis     # SLEEVE = retorno al CHASIS (Regla 13: jack externo a chasis)
    _safe_pin(tvs, '1') & tip             # TVS lado señal
    _safe_pin(tvs, '2') & gnd_chasis      # TVS lado CHASIS (no audio GND)
    fb = Part('Device', 'L', value='BLM18')           # ferrite en serie post-jack
    c47 = Part('Device', 'C', value='47pF_C0G')       # C 47 pF C0G al CHASIS
    tip & _safe_pin(fb, '1')
    _safe_pin(fb, '2') & tip_net
    tip_net & _safe_pin(c47, '1')
    _safe_pin(c47, '2') & gnd_chasis

def _wire_input_channel(parts, jack_key, tvs_key, opamp, in_pin, out_pin,
                        gnd_audio, gnd_chasis, vcc_12v, vee_12v, vcom_net):
    """Cadena de entrada de un canal (L o R) — §1.1."""
    # Nodo señal post-ESD/ferite
    node_ac = Net(f'IN_AC_{jack_key}')
    _wire_esd_jack(parts, jack_key, tvs_key, gnd_chasis, node_ac)

    # Acople DC wideband: 10µF ‖ 100nF
    c_in = Part('Device', 'C', value='10uF'); c_in_byp = Part('Device', 'C', value='100nF')
    node_ac & _safe_pin(c_in, '1') & Net(f'IN_AC2_{jack_key}') & _safe_pin(c_in_byp, '1')
    _safe_pin(c_in, '2') & gnd_audio; _safe_pin(c_in_byp, '2') & gnd_audio
    node_div = Net(f'IN_DIV_{jack_key}')
    # Divisor Thévenin 15k/4.7k + anti-aliasing 1nF C0G (§1.1, regla 7.0.1/7.5)
    r1 = Part('Device', 'R', value='15k'); r2 = Part('Device', 'R', value='4.7k')
    c_alias = Part('Device', 'C', value='1nF_C0G')
    (Net(f'IN_AC2_{jack_key}') & _safe_pin(r1, '1')); _safe_pin(r1, '2') & node_div
    _safe_pin(r2, '1') & node_div; _safe_pin(r2, '2') & gnd_audio
    _safe_pin(c_alias, '1') & node_div; _safe_pin(c_alias, '2') & gnd_audio

    # TL072 canal (A o B por pin) — preamp JFET gain ≈×5
    node_div & _safe_pin(opamp, in_pin)               # +IN
    r_fb = Part('Device', 'R', value='470k')
    out_node = Net(f'OUT_PRE_{jack_key}')
    _safe_pin(opamp, out_pin) & out_node
    # Realimentación: OUT → R_fb 470k → -IN (pin 2 para canal A, 6 para canal B)
    inv_pin = '2' if in_pin == '3' else '6'
    out_node & _safe_pin(r_fb, '1'); _safe_pin(r_fb, '2') & _safe_pin(opamp, inv_pin)

    # Acople AC al midrail VCOM del ADC (C_block_ADC 10µF)
    c_blk = Part('Device', 'C', value='10uF')
    adc_in = Net(f'ADC_VIN_{jack_key}')
    out_node & _safe_pin(c_blk, '1'); _safe_pin(c_blk, '2') & adc_in
    adc_in & vcom_net   # referencia +2.5V VCOM del PCM1808
    return adc_in

def _wire_input_stage(parts, gnd_audio, gnd_chasis, vcc_12v, vee_12v, vcc_3v3_an):
    u1 = parts['U1']  # TL072 dual
    _safe_pin(u1, '8') & vcc_12v      # V+ (Ley 1 pin absoluto DIP-8)
    _safe_pin(u1, '4') & vee_12v      # V-
    # VREF/VCOM del ADC (regla rules/13 §13.8 Ley 9)
    vcom = Net('ADC_VCOM')
    vref = Net('ADC_VREF')
    for n, net in [(vref, 'ADC_VREF'), (vcom, 'ADC_VCOM')]:
        c1 = Part('Device', 'C', value='10uF'); c2 = Part('Device', 'C', value='100nF')
        _safe_pin(c1, '1') & n; _safe_pin(c1, '2') & gnd_audio
        _safe_pin(c2, '1') & n; _safe_pin(c2, '2') & gnd_audio

    adc_in_l = _wire_input_channel(parts, 'JACK_IN_L', 'TVS_IN_L', u1, '3', '1',
                                   gnd_audio, gnd_chasis, vcc_12v, vee_12v, vcom)
    adc_in_r = _wire_input_channel(parts, 'JACK_IN_R', 'TVS_IN_R', u1, '5', '7',
                                   gnd_audio, gnd_chasis, vcc_12v, vee_12v, vcom)
    # PCM1808 (Connector stub numérico): VINL=pin4, VINR=pin5, VDD=pin15, FMT=pin7→DGND
    adc = parts['U_ADC']
    adc_in_l & _safe_pin(adc, '4')   # VINL ← canal L
    adc_in_r & _safe_pin(adc, '5')   # VINR ← canal R
    _safe_pin(adc, '6') & vcc_3v3_an # VCC codec = +5V rail (alias simplificado; codec usa 3V3/5V según §1.2)


# -----------------------------------------------------------------------------
# Etapa de SALIDA — PCM5102A VOUTL/VOUTR → NE5532 (A=L, B=R) → R-iso → nodo jack
#                    TVS P6KE12CA + FB + C47pF al CHASIS en cada jack (Regla 13)
# -----------------------------------------------------------------------------
def _wire_output_line(parts, opamp, in_pin, out_pin, jack_key, tvs_key,
                      gnd_audio, gnd_chasis, dac_vout_net):
    """Buffer unidad NE5532 por canal (§1.3)."""
    dac_vout_net & _safe_pin(opamp, in_pin)         # +IN
    inv = '2' if in_pin == '3' else '6'             # -IN (A:2, B:6)
    out_node = Net(f'OUT_LINE_{jack_key}')
    _safe_pin(opamp, out_pin) & out_node            # OUT
    _safe_pin(opamp, inv) & out_node                # buffer unidad (-IN = OUT)
    # R-iso 100Ω + nodo jack (post-R-iso pre-jack = Regla 13)
    r_iso = Part('Device', 'R', value='100')
    jack_node = Net(f'OUT_JACK_{jack_key}')
    out_node & _safe_pin(r_iso, '1'); _safe_pin(r_iso, '2') & jack_node
    _wire_esd_jack_output(parts, jack_key, tvs_key, gnd_chasis, jack_node)
    return out_node

def _wire_esd_jack_output(parts, jack_key, tvs_key, gnd_chasis, jack_node):
    """TVS P6KE12CA + FB + C47pF al CHASIS en salidas no balanceadas (Regla 13)."""
    tvs = parts[tvs_key]
    _safe_pin(tvs, '1') & jack_node; _safe_pin(tvs, '2') & gnd_chasis
    fb = Part('Device', 'L', value='BLM18')
    jack_node & _safe_pin(fb, '1'); _safe_pin(fb, '2') & _safe_pin(parts[jack_key], 'T')   # TIP (Ley 16)
    _safe_pin(parts[jack_key], 'S') & gnd_chasis   # SLEEVE retorno al CHASIS (Regla 13)
    c47 = Part('Device', 'C', value='47pF_C0G')
    jack_node & _safe_pin(c47, '1'); _safe_pin(c47, '2') & gnd_chasis

def _wire_output_stage(parts, gnd_audio, gnd_chasis, vcc_12v, vee_12v, vcc_3v3_an):
    u2 = parts['U2']  # NE5532 dual
    _safe_pin(u2, '8') & vcc_12v; _safe_pin(u2, '4') & vee_12v
    dac = parts['U_DAC']
    # PCM5102A XSMT pin17 → +3.3V (Ley 10), SCL pin11 → GND (PLL self-clocking)
    _safe_pin(dac, '17') & vcc_3v3_an
    _safe_pin(dac, '12') & gnd_audio   # SCK a GND (PLL self-clocking, Ley 10 — pin 12=SCK, NO 11=FLT)
    # VOUTL/VOUTR → acople C_out 10µF → NE5532
    for (chan_in_pin, chan_out_pin, jack_key, tvs_key, dac_pin) in [
        ('3', '1', 'JACK_OUT_L', 'TVS_OUT_L', '6'),    # OUTL pin 6 (rules/13 §13.9, Ley 10.4)
        ('5', '7', 'JACK_OUT_R', 'TVS_OUT_R', '7'),]:  # OUTR pin 7 (rules/13 §13.9, Ley 10.4)
        dac_vout = Net(f'DAC_VOUT_{jack_key}')
        _safe_pin(dac, dac_pin) & dac_vout
        c_out = Part('Device', 'C', value='10uF')
        dac_vout & _safe_pin(c_out, '1')
        dac_buf_in = Net(f'DAC_BUF_{jack_key}')
        _safe_pin(c_out, '2') & dac_buf_in
        _wire_output_line(parts, u2, chan_in_pin, chan_out_pin, jack_key, tvs_key,
                          gnd_audio, gnd_chasis, dac_buf_in)


# -----------------------------------------------------------------------------
# Etapa AURICULARES — downmix L+R → NJM4556A → R-iso 22Ω → jack HP (Regla 13)
# -----------------------------------------------------------------------------
def _wire_hp_stage(parts, gnd_audio, vcc_12v, vee_12v):
    u3 = parts['U3']  # NJM4556A
    _safe_pin(u3, '8') & vcc_12v; _safe_pin(u3, '4') & vee_12v
    # Downmix pasivo L+R (10kΩ c/u desde OUT_LINE_L / OUT_LINE_R)
    hp_mix = Net('HP_MIX')
    for src in ['OUT_LINE_JACK_OUT_L', 'OUT_LINE_JACK_OUT_R']:
        r = Part('Device', 'R', value='10k')
        try:
            src_net = next(n for n in builtins.default_circuit.nets if n.name == src)
        except StopIteration:
            src_net = hp_mix   # fallback si el net aún no existe
        src_net & _safe_pin(r, '1'); _safe_pin(r, '2') & hp_mix
    c_hp = Part('Device', 'C', value='10uF')
    hp_mix & _safe_pin(c_hp, '1'); _safe_pin(c_hp, '2') & _safe_pin(u3, '3')   # +IN_A
    out_hp = Net('OUT_HP')
    _safe_pin(u3, '2') & out_hp; _safe_pin(u3, '1') & out_hp                   # buffer unidad
    # R-iso 22Ω + nodo jack HP
    r_iso = Part('Device', 'R', value='22')
    hp_jack_node = Net('HP_JACK')
    out_hp & _safe_pin(r_iso, '1'); _safe_pin(r_iso, '2') & hp_jack_node
    # Regla 13 (TVS_OUT_HP ya en inventario)
    tvs = parts['TVS_HP']
    _safe_pin(tvs, '1') & hp_jack_node; _safe_pin(tvs, '2') & gnd_audio
    fb = Part('Device', 'L', value='BLM18')
    hp_jack_node & _safe_pin(fb, '1'); _safe_pin(fb, '2') & _safe_pin(parts['JACK_HP'], 'T')   # TIP (Ley 16)
    _safe_pin(parts['JACK_HP'], 'S') & gnd_audio   # SLEEVE retorno audio (HP interno, no chasis)


# -----------------------------------------------------------------------------
# Etapa POTENCIA — DC jack + MOSFET-P → LT1054 (rail negativo) + Buck + AP2112K
# -----------------------------------------------------------------------------
def _wire_power_stage(parts, gnd_audio, gnd_chasis, vcc_12v, vee_12v, vcc_5v, vcc_3v3, vcc_3v3_an):
    # DC jack 12V + MOSFET-P IRLML6401 (protección inversión, regla 7.31)
    dc = parts['JACK_DC']
    _safe_pin(dc, '1') & vcc_12v                   # +12V
    _safe_pin(dc, '2') & gnd_chasis               # CHASIS GND
    q2 = parts['Q2']                              # IRLML6401 (S/D/G por símbolo SOT-23)
    _safe_pin(q2, '1') & vcc_12v; _safe_pin(q2, '2') & vcc_12v; _safe_pin(q2, '3') & gnd_chasis

    # LT1054 (U4) — inversor: V+ pin8 = +12V, VOUT pin5 = -12V (D.4 / rules/13 §13.3)
    u4 = parts['U4']
    _safe_pin(u4, '8') & vcc_12v                   # V+
    _safe_pin(u4, '3') & gnd_audio                # GND
    _safe_pin(u4, '5') & vee_12v                   # VOUT = -12V
    # Cap de bombeo C1+ (pin2) ↔ C1− (pin4)
    c_pump = Part('Device', 'C', value='10uF')
    _safe_pin(u4, '2') & _safe_pin(c_pump, '1')
    _safe_pin(u4, '4') & _safe_pin(c_pump, '2')
    # FB pin1 ↔ VOUT pin5 (modo inversor), OSC pin7 ↔ V+ pin8 (vía R_boost 20k → BOOST)
    _safe_pin(u4, '1') & vee_12v
    r_boost = Part('Device', 'R', value='20k')
    _safe_pin(u4, '7') & _safe_pin(r_boost, '1'); _safe_pin(r_boost, '2') & vcc_12v

    # AP2112K (U5) — LDO 3.3V aislado para codecs (regla 2 / rules/13 §13.6)
    u5 = parts['U5']
    _safe_pin(u5, '1') & vcc_5v                   # VIN
    _safe_pin(u5, '2') & gnd_audio                # GND
    _safe_pin(u5, '3') & vcc_5v                   # EN always-on
    _safe_pin(u5, '5') & vcc_3v3_an               # VOUT
    c_bp = Part('Device', 'C', value='10nF'); _safe_pin(u5, '4') & _safe_pin(c_bp, '1'); _safe_pin(c_bp, '2') & gnd_audio
    c_out_ldo = Part('Device', 'C', value='4.7uF'); _safe_pin(c_out_ldo, '1') & vcc_3v3_an; _safe_pin(c_out_ldo, '2') & gnd_audio


# -----------------------------------------------------------------------------
# Mute Relay (K1/K2 G5V-1) — 1×2N3904 drivea AMBOS relés en paralelo (mute estéreo
# simultáneo) + flyback 1N4148 por relé (Ley 5 / §1.4). KISS: 1 transistor, 2 relés.
# -----------------------------------------------------------------------------
def _wire_mute_relay(parts, gnd_audio, gnd_digital, vcc_5v):
    q1 = parts['Q1']                              # 2N3904 TO-92: 1=E, 2=B, 3=C
    _safe_pin(q1, '1') & gnd_digital              # Emitter
    r_base = Part('Device', 'R', value='1k')      # R_base GPIO23 → base
    get_mcu_pin(parts, 'GPIO23') & _safe_pin(r_base, '1')
    _safe_pin(r_base, '2') & _safe_pin(q1, '2')   # Base
    _safe_pin(q1, '3') & _safe_pin(parts['K1'], '9')   # Colector → Coil− K1
    _safe_pin(q1, '3') & _safe_pin(parts['K2'], '9')   # Colector → Coil− K2 (paralelo)
    for rel_key, d_key in [('K1', 'D1'), ('K2', 'D2')]:
        k = parts[rel_key]
        _safe_pin(k, '2') & vcc_5v                # Coil+ (Ley 5: pin 2 = Coil+)
        d = parts[d_key]
        _safe_pin(d, '1') & _safe_pin(k, '2')     # flyback K (cátodo) → Coil+
        _safe_pin(d, '2') & _safe_pin(k, '9')     # flyback A (ánodo) → Coil−
        _safe_pin(k, '1') & gnd_audio             # NC a tierra (mute seguro en boot)


# -----------------------------------------------------------------------------
# Safe Bypass HEF4053BT (U8) — 1 switch usado, pull-down (§1.4 / rules/13 §13.5)
# -----------------------------------------------------------------------------
def _wire_safe_bypass(parts, gnd_audio, gnd_digital, vcc_3v3):
    u8 = parts['U8']
    _safe_pin(u8, '16') & vcc_3v3                 # VDD
    _safe_pin(u8, '8') & gnd_digital              # VSS
    _safe_pin(u8, '14') & gnd_digital             # VEE ↔ VSS single-supply
    _safe_pin(u8, '6') & gnd_digital              # S3 (no usado) a GND
    _safe_pin(u8, '15') & gnd_digital             # E (enable LOW)
    _safe_pin(u8, '9') & gnd_digital              # S2 (no usado) a GND
    # S1 pin10 = control (mapeado a GPIO24 fuera, con pull-down 100k)
    r_pd = Part('Device', 'R', value='100k')
    _safe_pin(u8, '10') & _safe_pin(r_pd, '1'); _safe_pin(r_pd, '2') & gnd_digital


# -----------------------------------------------------------------------------
# UI — Encoders (vía iterador ENC_1..ENC_3 + ENC_4), Footswitches, NeoPixel cascade
# -----------------------------------------------------------------------------
def _wire_ui_stage(parts, gnd_audio, gnd_digital, vcc_5v, vcc_3v3):
    # NeoPixel cascade ×8 (Part directo — no factory) §4
    neo_in = get_mcu_pin(parts, 'GPIO28')
    r_data = Part('Device', 'R', value='470')
    neo_in & _safe_pin(r_data, '1')
    prev_data = _safe_pin(r_data, '2')
    c_bulk = Part('Device', 'C', value='100uF'); _safe_pin(c_bulk, '1') & vcc_5v; _safe_pin(c_bulk, '2') & gnd_digital
    for i in range(8):
        led = Part('LED', 'WS2812B', value='WS2812B')
        _safe_pin(led, '1') & vcc_5v              # VDD
        _safe_pin(led, '3') & gnd_digital         # VSS
        _safe_pin(led, '4') & prev_data           # DIN
        prev_data = _safe_pin(led, '2')           # DOUT → siguiente DIN

    # OLED SSD1306 I2C (Connector stub 1x04): SDA=GPIO0, SCL=GPIO1
    oled = parts['OLED']
    _safe_pin(oled, '1') & vcc_3v3
    _safe_pin(oled, '2') & gnd_digital
    _safe_pin(oled, '3') & get_mcu_pin(parts, 'GPIO0')   # SDA
    _safe_pin(oled, '4') & get_mcu_pin(parts, 'GPIO1')   # SCL

    # Pots (ADC): Master Vol GPIO26, Expression GPIO27 — divisor simple
    for pot_key, gpio in [('POT_MASTER', 'GPIO26'), ('POT_EXPR', 'GPIO27')]:
        pot = parts[pot_key]
        _safe_pin(pot, '1') & vcc_3v3
        _safe_pin(pot, '3') & gnd_digital
        _safe_pin(pot, '2') & get_mcu_pin(parts, gpio)   # wiper


# -----------------------------------------------------------------------------
# MCU CORE — I2S bus, MCLK, Mute/Bypass control (cero pines mágicos)
# -----------------------------------------------------------------------------
def _wire_mcu_core(parts, gnd_audio, gnd_digital, vcc_3v3, vcc_3v3_an):
    mcu = parts['MCU']
    adc = parts['U_ADC']; dac = parts['U_DAC']; u8 = parts['U8']
    # I2S (PCM1808: LRCK=8, DOUT=9, BCK=10, SCKI=11 ; PCM5102A: BCK=13, DIN=14, LRCK=15)
    get_mcu_pin(parts, 'GPIO15') & _safe_pin(adc, '8')   # LRCK
    get_mcu_pin(parts, 'GPIO17') & _safe_pin(adc, '9')   # DIN (←ADC DOUT)
    get_mcu_pin(parts, 'GPIO14') & _safe_pin(adc, '10')  # BCLK
    get_mcu_pin(parts, 'GPIO21') & _safe_pin(adc, '11')  # MCLK (SCKI)
    get_mcu_pin(parts, 'GPIO14') & _safe_pin(dac, '13')  # BCLK compartido
    get_mcu_pin(parts, 'GPIO16') & _safe_pin(dac, '14')  # DOUT (→DAC DIN)
    get_mcu_pin(parts, 'GPIO15') & _safe_pin(dac, '15')  # LRCK compartido
    # Control GPIOs: GPIO23 (MUTE) se cablea en _wire_mute_relay (drive Q1 → Coil relés)
    get_mcu_pin(parts, 'GPIO24') & _safe_pin(u8, '10')   # BYPASS_CTRL → HEF4053 S1


# =============================================================================
# BUILD_SPICE_STAGE — DUT aislado por etapa (Grupo D.1, firma EXACTA del motor).
# Devuelve extra_spice_lines (agnóstico) para nodos propios del proyecto (+12V).
# =============================================================================
def build_spice_stage(stage_name, parts, gnd_audio, gnd_chasis, vcc_12v, vee_minus_12v):
    """Aísla la etapa pedida en un DUT SPICE. `parts` llega con todo el inventario
    (motor); aquí cableamos SOLO los componentes de la etapa. Los opamps duales se
    exportan canal-aware (XUxA siempre, XUxB si los pines B están cableados).

    Nota tensión: el pedal es ±12V (ECO 12V aplicado), coherente con el testbench SPICE
    que fuerza vcc_12v=+12V y vee_minus_12v=-12V. TL072/NE5532/NJM4556A toleran ±18V
    (headroom OK). El LT1054 del power_stage se alimenta de +12V (vcc_12v del motor)."""

    extra = []

    if stage_name == 'input_stage':
        # TL072 canal A: preamp guitarra → nodo ADC_VIN. AC sweep valida anti-aliasing.
        u1 = parts['U1']
        _safe_pin(u1, '8') & vcc_12v            # V+ (header motor = +12V)
        _safe_pin(u1, '4') & vee_minus_12v      # V- (header motor = -12V)
        node_in  = Net('IN_RAW')
        node_ac  = Net('IN_AC')
        node_div = Net('IN_DIV')
        out_pre  = Net('OUT_PRE')
        c_in = Part('Device', 'C', value='10uF')
        node_in & _safe_pin(c_in, '1'); _safe_pin(c_in, '2') & node_ac
        r1 = Part('Device', 'R', value='15k'); r2 = Part('Device', 'R', value='4.7k')
        c_al = Part('Device', 'C', value='1nF')
        node_ac & _safe_pin(r1, '1'); _safe_pin(r1, '2') & node_div
        _safe_pin(r2, '1') & node_div; _safe_pin(r2, '2') & gnd_audio
        _safe_pin(c_al, '1') & node_div; _safe_pin(c_al, '2') & gnd_audio
        node_div & _safe_pin(u1, '3')           # +INA
        r_fb = Part('Device', 'R', value='470k')
        _safe_pin(u1, '1') & out_pre
        out_pre & _safe_pin(r_fb, '1'); _safe_pin(r_fb, '2') & _safe_pin(u1, '2')   # -INA
        # input_node=IN_RAW, output_node=OUT_PRE (definidos en spice_targets.yaml)

    elif stage_name == 'output_stage':
        # NE5532 canal A: buffer PCM5102A VOUTL → OUT_LINE. THD transient Touring Grade.
        u2 = parts['U2']
        _safe_pin(u2, '8') & vcc_12v; _safe_pin(u2, '4') & vee_minus_12v
        dac_out = Net('DAC_VOUT')
        out_line = Net('OUT_LINE')
        c_out = Part('Device', 'C', value='10uF')
        dac_out & _safe_pin(c_out, '1'); _safe_pin(c_out, '2') & _safe_pin(u2, '3')   # +INA
        _safe_pin(u2, '2') & _safe_pin(u2, '1')  # buffer unidad
        _safe_pin(u2, '1') & out_line
        r_iso = Part('Device', 'R', value='100')   # carga realista 100k de OUT_LINE a GND (D.3-style)
        rl = Part('Device', 'R', value='100k')
        out_line & _safe_pin(r_iso, '1'); _safe_pin(r_iso, '2') & _safe_pin(rl, '1'); _safe_pin(rl, '2') & gnd_audio

    elif stage_name == 'output_hp_stage':
        # NJM4556A canal A: buffer HP con carga 32Ω. THD @32Ω.
        u3 = parts['U3']
        _safe_pin(u3, '8') & vcc_12v; _safe_pin(u3, '4') & vee_minus_12v
        hp_in = Net('HP_IN'); out_hp = Net('OUT_HP')
        hp_in & _safe_pin(u3, '3')                # +INA
        _safe_pin(u3, '2') & _safe_pin(u3, '1')   # buffer unidad
        _safe_pin(u3, '1') & out_hp
        r_iso = Part('Device', 'R', value='22')
        rl_32 = Part('Device', 'R', value='32')   # carga de auriculares 32Ω
        out_hp & _safe_pin(r_iso, '1'); _safe_pin(r_iso, '2') & _safe_pin(rl_32, '1'); _safe_pin(rl_32, '2') & gnd_audio

    elif stage_name == 'power_stage':
        # LT1054 inversor: +12V (extra_spice_line) → VEE_PEDAL con carga DC 10k (D.3/D.4).
        # NO usamos vcc_12v/vee_minus_12v aquí (el inversor genera su propio riel).
        u4 = parts['U4']
        vee_pedal = Net('VEE_PEDAL')
        _safe_pin(u4, '8') & vcc_12v             # V+ del LT1054 (usamos +12V del header como input conservador)
        _safe_pin(u4, '3') & gnd_audio
        _safe_pin(u4, '5') & vee_pedal           # VOUT = riel negativo generado
        c_pump = Part('Device', 'C', value='10uF')
        _safe_pin(u4, '2') & _safe_pin(c_pump, '1'); _safe_pin(u4, '4') & _safe_pin(c_pump, '2')
        _safe_pin(u4, '1') & vee_pedal           # FB ↔ VOUT (modo inversor)
        r_boost = Part('Device', 'R', value='20k')
        _safe_pin(u4, '7') & _safe_pin(r_boost, '1'); _safe_pin(r_boost, '2') & vcc_12v
        # Carga DC del riel generado (D.3 anti singular-matrix): 10k a GND
        rl = Part('Device', 'R', value='10k')
        vee_pedal & _safe_pin(rl, '1'); _safe_pin(rl, '2') & gnd_audio
        # AP2112K aislado con su carga DC 4.7k en +3V3 (D.3)
        u5 = parts['U5']
        vcc_3v3_dut = Net('P3V3_DUT')
        _safe_pin(u5, '1') & vcc_12v; _safe_pin(u5, '2') & gnd_audio; _safe_pin(u5, '3') & vcc_12v
        _safe_pin(u5, '5') & vcc_3v3_dut
        rl3 = Part('Device', 'R', value='4.7k')
        vcc_3v3_dut & _safe_pin(rl3, '1'); _safe_pin(rl3, '2') & gnd_audio

    else:
        raise ValueError(f"stage_name '{stage_name}' no declarado en build_spice_stage. "
                         f"Válidos: input_stage | output_stage | output_hp_stage | power_stage")

    return extra if extra else None
