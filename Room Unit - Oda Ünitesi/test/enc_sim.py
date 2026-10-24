# main.cpp'deki enkoder okumasının masaüstü simülasyonu:
# 1 ms örnekleme + ENC_STABLE_SAMPLES kararlılık filtresi + ENC_TABLE durum makinesi.
# Çalıştırma: python enc_sim.py
import random

R_START, R_CW_FINAL, R_CW_BEGIN, R_CW_NEXT, R_CCW_BEGIN, R_CCW_FINAL, R_CCW_NEXT = range(7)
DIR_CW, DIR_CCW = 0x10, 0x20
T = [
    [R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START],
    [R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW],
    [R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START],
    [R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START],
    [R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START],
    [R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW],
    [R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START],
]
STABLE = 3  # ENC_STABLE_SAMPLES

def detent_ms(cw, step_ms, noise_ms):
    """Bir tık için milisaniyelik (CLK<<1|DT) örnekleri; her geçişten sonra gürültü."""
    seq = [0b01, 0b00, 0b10, 0b11] if cw else [0b10, 0b00, 0b01, 0b11]
    out, cur = [], 0b11
    for nxt in seq:
        for _ in range(random.randint(0, noise_ms)):   # geçiş sırasında rastgele titreme
            out.append(random.choice([cur, nxt]))
        out += [nxt] * step_ms
        cur = nxt
    return out

def run(samples):
    raw_prev, stable_pins, count, st, ticks = 0b11, 0b11, 0, R_START, 0
    for pins in samples:                      # her eleman = bir 1 ms zamanlayıcı kesmesi
        if pins != raw_prev:
            raw_prev, count = pins, 0
        elif count < STABLE:
            count += 1
            if count == STABLE and pins != stable_pins:
                stable_pins = pins
                st = T[st & 0x0F][pins]
                if st & 0x30 == DIR_CW: ticks += 1
                elif st & 0x30 == DIR_CCW: ticks -= 1
    return ticks, st & 0x0F

random.seed(1)
cases = [
    ('yavas 5 ileri',        [True] * 5,                   40, 2),
    ('yavas 5 geri',         [False] * 5,                  40, 2),
    ('3 ileri 2 geri',       [True] * 3 + [False] * 2,     30, 2),
    ('hizli 20 ileri (6ms)', [True] * 20,                  6,  2),
    ('hizli 20 geri (6ms)',  [False] * 20,                 6,  2),
]
bad = 0
for name, plan, step, noise in cases:
    samples = [s for cw in plan for s in detent_ms(cw, step, noise)] + [0b11] * 10
    exp = sum(1 if cw else -1 for cw in plan)
    got, st = run(samples)
    ok = got == exp and st == R_START
    bad += not ok
    print(f'{name:22s} ornek={len(samples):5d}  sayilan={got:+3d}  beklenen={exp:+3d}  {"OK" if ok else "HATA"}')
print('SONUC:', 'hepsi gecti' if bad == 0 else f'{bad} senaryo basarisiz')
