#!/usr/bin/env python3
# pip install python-osc
# Usage: python eye_osc_sim.py --host 127.0.0.1 --port 9000 --hz 60
# Sends:
#   /avatar/parameters/v2/EyeLeftX,  v2/EyeLeftY         [-1..1]
#   /avatar/parameters/v2/EyeRightX, v2/EyeRightY        [-1..1]
#   /avatar/parameters/v2/EyeLidLeft,  v2/EyeLidRight    [0..1]  (0=closed..0.75=open..>0.75=widen)
#   /avatar/parameters/v2/EyeSquintLeft, v2/EyeSquintRight [0..1]
#   /avatar/parameters/v2/PupilDilation                  [0..1]
#   /avatar/parameters/v2/BrowInnerUp{Left,Right}        [0..1]
#   /avatar/parameters/v2/BrowOuterUp{Left,Right}        [0..1]
#   /avatar/parameters/v2/BrowLowerer{Left,Right}        [0..1]
# Optional debug:
#   /avatar/parameters/Mood                              [-1..1]

import argparse, math, random, time
from pythonosc.udp_client import SimpleUDPClient

def clamp(x,a,b): return a if x<a else b if x>b else x
def smooth01(t): t=clamp(t,0.0,1.0); return t*t*(3-2*t)

class OU:
    def __init__(self, mu=0.0, theta=2.0, sigma=0.3):
        self.x=mu; self.mu=mu; self.theta=theta; self.sigma=sigma
    def step(self, dt):
        self.x += self.theta*(self.mu-self.x)*dt + self.sigma*math.sqrt(max(dt,1e-4))*random.gauss(0,1)
        return self.x

class EyeSim:
    def __init__(self):
        self.t=0.0
        self.L={'pos':[0.0,0.0], 'start':[0.0,0.0], 'target':[0.0,0.0]}
        self.R={'pos':[0.0,0.0], 'start':[0.0,0.0], 'target':[0.0,0.0]}
        self.sacc_t=1.0; self.sacc_dur=0.05; self.next_sacc=0.0
        self.ouLx, self.ouLy = OU(0,1.5,0.08), OU(0,1.5,0.08)
        self.ouRx, self.ouRy = OU(0,1.5,0.08), OU(0,1.5,0.08)
        self.pupilOU = OU(0.55, 1.2, 0.04)
        self.mood_phase = random.random()*math.tau
        self.mood_speed = 0.08
        self.mood_amp = 0.7
        self.blink_t=1.0; self.blink_dur=0.16; self.next_blink=0.5+random.random()*3.5
        self.micro_p=0.15

    def _new_gaze(self):
        base_x = random.uniform(-0.6, 0.6)
        base_y = random.uniform(-0.4, 0.4)
        verg   = random.uniform(0.0, 0.08)
        self.L['start']=self.L['pos'][:]; self.R['start']=self.R['pos'][:]
        self.L['target']=[clamp(base_x-verg,-1,1), clamp(base_y,-1,1)]
        self.R['target']=[clamp(base_x+verg,-1,1), clamp(base_y,-1,1)]
        self.sacc_dur = random.uniform(0.03, 0.07)
        self.sacc_t=0.0
        self.next_sacc = self.t + random.uniform(0.18, 1.1)

    def _tick_gaze(self, dt):
        if self.sacc_t < 1.0:
            self.sacc_t += dt/self.sacc_dur
            a = smooth01(self.sacc_t)
            for i in (0,1):
                self.L['pos'][i] = (1-a)*self.L['start'][i] + a*self.L['target'][i]
                self.R['pos'][i] = (1-a)*self.R['start'][i] + a*self.R['target'][i]
        else:
            self.L['pos'][0] = clamp(self.L['pos'][0] + 0.05*self.ouLx.step(dt), -1, 1)
            self.L['pos'][1] = clamp(self.L['pos'][1] + 0.05*self.ouLy.step(dt), -1, 1)
            self.R['pos'][0] = clamp(self.R['pos'][0] + 0.05*self.ouRx.step(dt), -1, 1)
            self.R['pos'][1] = clamp(self.R['pos'][1] + 0.05*self.ouRy.step(dt), -1, 1)

    def _maybe_blink(self):
        if self.t >= self.next_blink and self.blink_t >= 1.0:
            self.blink_dur = 0.12 if random.random()<self.micro_p else 0.18
            self.blink_t = 0.0
            self.next_blink = self.t + random.uniform(2.2, 5.0)

    def _blink_amt(self, dt):
        if self.blink_t < 1.0:
            self.blink_t += dt/self.blink_dur
            u = clamp(self.blink_t, 0.0, 1.0)
            return smooth01(1.0 - abs(2*u - 1.0))  # 0..1
        return 0.0

    def step(self, dt):
        self.t += dt
        if self.t >= self.next_sacc and self.sacc_t >= 1.0:
            self._new_gaze()
        self._tick_gaze(dt)

        # mood ∈ [-1,1]
        self.mood_phase += self.mood_speed*dt
        mood = clamp(self.mood_amp*math.sin(self.mood_phase), -1.0, 1.0)

        # blink with upper-lid dominance
        self._maybe_blink()
        b = self._blink_amt(dt)

        # pupil dilation [0..1]
        pupil = clamp(self.pupilOU.step(dt), 0.2, 0.95)

        # vertical gaze + mood -> lid openness and squint (upper-lid emphasis)
        def lid_open_for(y, mood):
            up = max(0.0,  mood); dn = max(0.0, -mood)
            base = 0.9 + 0.25*y + 0.10*up - 0.10*dn  # openness before blink
            open_after = clamp(base*(1.0 - 0.85*b), 0.0, 1.2)  # >1 => widen
            squint = 1.0 #clamp(0.9*b + 0.1*dn + 0.15*max(0.0, -y), 0.0, 1.0)
            return open_after, squint

        L_open, L_squint = lid_open_for(self.L['pos'][1], mood)
        R_open, R_squint = lid_open_for(self.R['pos'][1], mood)

        def to_v2_eyelid(val):
            if val <= 1.0:
                return clamp(0.75*val, 0.0, 0.75)
            extra = clamp(val-1.0, 0.0, 1.0)
            return clamp(0.75 + 0.25*extra, 0.0, 1.0)

        return {
            "gazeL": tuple(self.L['pos']),
            "gazeR": tuple(self.R['pos']),
            "pupil": float(pupil),
            "blink": float(b),
            "mood":  float(mood),
            "L_eyelid": float(to_v2_eyelid(L_open)),
            "R_eyelid": float(to_v2_eyelid(R_open)),
            "L_squint": float(L_squint),
            "R_squint": float(R_squint),
        }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--hz", type=float, default=60.0)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--send-mood", action="store_true",
                    help="Also send /avatar/parameters/Mood [-1..1]")
    ap.add_argument("--print-every", type=float, default=1.0)
    args = ap.parse_args()
    if args.seed is not None:
        random.seed(args.seed)

    client = SimpleUDPClient(args.host, args.port)
    sim = EyeSim()
    dt = 1.0/max(1.0, args.hz)
    next_log = time.perf_counter() + max(0.0, args.print_every)
    last = time.perf_counter()

    try:
        while True:
            now = time.perf_counter()
            sleep_t = dt - (now - last)
            if sleep_t > 0: time.sleep(sleep_t)
            cur = time.perf_counter()
            frame_dt = cur - last
            last = cur

            s = sim.step(frame_dt)

            Lx, Ly = s["gazeL"]; Rx, Ry = s["gazeR"]
            client.send_message("/avatar/parameters/v2/EyeLeftX",  float(Lx))
            client.send_message("/avatar/parameters/v2/EyeLeftY",  float(Ly))
            client.send_message("/avatar/parameters/v2/EyeRightX", float(Rx))
            client.send_message("/avatar/parameters/v2/EyeRightY", float(Ry))
            client.send_message("/avatar/parameters/v2/PupilDilation", float(s["pupil"]))
            client.send_message("/avatar/parameters/v2/EyeLidLeft",  float(s["L_eyelid"]))
            client.send_message("/avatar/parameters/v2/EyeLidRight", float(s["R_eyelid"]))
            client.send_message("/avatar/parameters/v2/EyeSquintLeft",  float(s["L_squint"]))
            client.send_message("/avatar/parameters/v2/EyeSquintRight", float(s["R_squint"]))

            mood = s["mood"]; up, dn = max(0.0, mood), max(0.0, -mood)
            for side in ("Left","Right"):
                client.send_message(f"/avatar/parameters/v2/BrowInnerUp{side}",  float(0.5*up))
                client.send_message(f"/avatar/parameters/v2/BrowOuterUp{side}",  float(up))
                client.send_message(f"/avatar/parameters/v2/BrowLowerer{side}",  float(dn))
            if args.send_mood:
                client.send_message("/avatar/parameters/Mood", float(mood))

            if args.print_every > 0 and cur >= next_log:
                next_log = cur + args.print_every
                print(f"L({Lx:+.2f},{Ly:+.2f}) R({Rx:+.2f},{Ry:+.2f}) "
                      f"pupil={s['pupil']:.2f} blink={s['blink']:.2f} mood={mood:+.2f} "
                      f"LidL={s['L_eyelid']:.2f}/{s['L_squint']:.2f} "
                      f"LidR={s['R_eyelid']:.2f}/{s['R_squint']:.2f}")
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
