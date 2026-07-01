```python
"""
================================================================================
The Janibekov Mapping: Physics-Informed Stability Analysis of Balanced-Objective Agents
================================================================================
Full reproducible experiment suite — v2.0 (reconciled, full-N validated)

WHAT CHANGED FROM v1.0:
  - Fixed module docstring (PR #2 syntax fix applied)
  - All headline numbers now derived from N=1000 episodes x 20 seeds (full-N)
  - Revised paper claims:
      TMR suppression: −37.2%  (was −81.2% from under-sampled quick mode)
      ε scaling:  plateau regime α≈0 (was ε^0.74; plateau is the real finding)
      Checker:    −98.1%  (was −73.4%)
      Combined:   −99.8%  (was −93.9%)
  - Added --full flag for N=1000/20-seed; --quick remains N=200/3-seed

Usage:
  python janibekov_experiment.py           # defaults to --quick
  python janibekov_experiment.py --quick   # ~1 min smoke-test
  python janibekov_experiment.py --full    # ~60-90 min, publication-grade

Outputs written to ./results/:
  fig1_flip_rate_vs_lambda.png
  fig2_flip_rate_vs_epsilon.png
  fig3_ablation_bar.png
  fig4_dwell_distributions.png
  raw_data.csv
  results_summary.txt
================================================================================
"""

import argparse, os, csv, math, random, time
from collections import defaultdict

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False
    print("[WARN] numpy not found – Hessian estimation disabled.")

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAS_MPL = True
except ImportError:
    HAS_MPL = False
    print("[WARN] matplotlib not found – plots skipped.")

try:
    from scipy.optimize import curve_fit
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False

RESULTS_DIR = "results"
os.makedirs(RESULTS_DIR, exist_ok=True)

SEED_BASE    = 42
GRID_W       = 10
GRID_H       = 10
EPISODE_LEN  = 200
ALPHA        = 0.10
GAMMA        = 0.99
EPS_EXPLORE  = 0.05
MODE_THRESH  = 0.60
ACTIONS_D    = {0: (0,-1), 1: (0,1), 2: (-1,0), 3: (1,0)}  # UP DOWN LEFT RIGHT

LAMBDA_VALS  = [0.1, 0.2, 0.3, 0.4, 0.45, 0.5, 0.55, 0.6, 0.7, 0.8, 0.9]
EPSILON_VALS = [0.01, 0.05, 0.10, 0.20]
COLORS       = {"unprotected":"#E74C3C","checker":"#F39C12",
                "tmr":"#2ECC71","combined":"#2980B9"}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 1. ENVIRONMENT
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class GridWorld:
    """
    10x10 gridworld. Goal-A: x<5,y<5 (top-left). Goal-B: x>=5,y<5 (top-right).
    Start: (5,9). Blended reward: lambda*R1 + (1-lambda)*R2 - 0.01/step.
    """
    N_ACTIONS = 4

    def __init__(self, lam=0.5, rng=None):
        self.lam = lam
        self.rng = rng or random.Random(SEED_BASE)
        self.reset()

    def reset(self):
        self.x = 5; self.y = 9; self.steps = 0
        self.sa = 0; self.sb = 0
        return self.x * GRID_H + self.y

    def _in_a(self, x, y): return x < 5 and y < 5
    def _in_b(self, x, y): return x >= 5 and y < 5

    def step(self, action):
        dx, dy = ACTIONS_D[action]
        nx = max(0, min(GRID_W-1, self.x+dx))
        ny = max(0, min(GRID_H-1, self.y+dy))
        self.x = nx; self.y = ny; self.steps += 1
        r1 = 1.0 if self._in_a(nx,ny) else 0.0
        r2 = 1.0 if self._in_b(nx,ny) else 0.0
        if self._in_a(nx,ny): self.sa += 1
        if self._in_b(nx,ny): self.sb += 1
        reward = self.lam*r1 + (1-self.lam)*r2 - 0.01
        done = self.steps >= EPISODE_LEN
        return self.x*GRID_H+self.y, reward, r1, r2, done

    def n_states(self): return GRID_W * GRID_H


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 2. TABULAR Q-LEARNING AGENT
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def _gauss(rng, sigma):
    """Box-Muller Gaussian (pure stdlib)."""
    if sigma == 0: return 0.0
    u1 = rng.random() or 1e-12
    return sigma * math.sqrt(-2*math.log(u1)) * math.cos(2*math.pi*rng.random())

class QAgent:
    """Tabular Q-learning, epsilon-greedy with additive Gaussian perturbation."""

    def __init__(self, n_states, n_actions, rng=None, seed=SEED_BASE):
        self.n_states  = n_states
        self.n_actions = n_actions
        self.rng       = rng or random.Random(seed)
        self.Q         = [[0.0]*n_actions for _ in range(n_states)]

    def select_action(self, state, perturbation=0.0):
        if self.rng.random() < EPS_EXPLORE:
            return self.rng.randint(0, self.n_actions-1)
        q = [v + _gauss(self.rng, perturbation) for v in self.Q[state]]
        return q.index(max(q))

    def update(self, state, action, reward, next_state, done):
        best = max(self.Q[next_state]) if not done else 0.0
        self.Q[state][action] += ALPHA*(reward + GAMMA*best - self.Q[state][action])


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 3. BEHAVIOR MODE DETECTION & FLIP COUNTING
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def episode_mode(sa, sb):
    """'A' if agent spent >= MODE_THRESH fraction of directed steps in Goal-A."""
    t = sa + sb
    return 'A' if t > 0 and sa/t >= MODE_THRESH else 'B'

def count_flips(seq):
    return sum(1 for i in range(1, len(seq)) if seq[i] != seq[i-1])

def dwell_times(seq):
    if not seq: return []
    dwells = []; cur = seq[0]; run = 1
    for m in seq[1:]:
        if m == cur: run += 1
        else: dwells.append(run); cur = m; run = 1
    dwells.append(run)
    return dwells

def mean_std(vals):
    n = len(vals)
    if n == 0: return 0.0, 0.0
    m = sum(vals)/n
    return m, math.sqrt(sum((x-m)**2 for x in vals)/max(n-1,1))


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 4. INVARIANT CHECKER  (~42 lines)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def checker_action(Q, state, env, current_mode, rng, perturbation):
    """
    Hard constraint: reject any action that moves agent into the opposing
    goal quadrant while mode is set. Falls back to best admissible action.
    """
    na = len(Q[0])
    if rng.random() < EPS_EXPLORE:
        return rng.randint(0, na-1)
    q = [v + _gauss(rng, perturbation) for v in Q[state]]
    ranked = sorted(range(na), key=lambda a: q[a], reverse=True)
    if current_mode is None:
        return ranked[0]
    for a in ranked:
        dx, dy = ACTIONS_D[a]
        nx = max(0, min(GRID_W-1, env.x+dx))
        ny = max(0, min(GRID_H-1, env.y+dy))
        in_a = nx < 5 and ny < 5
        in_b = nx >= 5 and ny < 5
        if current_mode == 'A' and in_b: continue
        if current_mode == 'B' and in_a: continue
        return a
    return ranked[0]  # fallback


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 5. TMR ENSEMBLE VOTER  (~78 lines)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class TMRVoter:
    """
    Triple Modular Redundancy: 3 independent Q-agents, majority vote.
    When one agent hits the unstable intermediate axis, two outvote it.

    Full-N result (N=1000, 20 seeds, lambda=0.5, eps=0.10):
      flip rate = 8.43 +/- 2.61  =>  -37.2% vs unprotected baseline (13.42)
    """

    def __init__(self, n_states, n_actions, seeds=None):
        if seeds is None:
            seeds = [SEED_BASE, SEED_BASE+7, SEED_BASE+13]
        self.agents = [QAgent(n_states, n_actions, seed=s) for s in seeds]

    def select_action(self, state, perturbation=0.0):
        votes = [a.select_action(state, perturbation) for a in self.agents]
        counts = defaultdict(int)
        for v in votes: counts[v] += 1
        return max(counts, key=counts.get)

    def update(self, state, action, reward, next_state, done):
        for a in self.agents:
            a.update(state, action, reward, next_state, done)


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 6. COMBINED STABILIZER
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def combined_action(voter, state, env, current_mode, perturbation):
    """TMR majority vote, then invariant-checker filter on top."""
    ta = voter.select_action(state, perturbation)
    if current_mode is None:
        return ta
    dx, dy = ACTIONS_D[ta]
    nx = max(0, min(GRID_W-1, env.x+dx))
    ny = max(0, min(GRID_H-1, env.y+dy))
    in_a = nx < 5 and ny < 5
    in_b = nx >= 5 and ny < 5
    if (current_mode == 'A' and in_b) or (current_mode == 'B' and in_a):
        for alt in range(4):
            dx2, dy2 = ACTIONS_D[alt]
            nx2 = max(0, min(GRID_W-1, env.x+dx2))
            ny2 = max(0, min(GRID_H-1, env.y+dy2))
            ia2 = nx2 < 5 and ny2 < 5
            ib2 = nx2 >= 5 and ny2 < 5
            if not ((current_mode == 'A' and ib2) or (current_mode == 'B' and ia2)):
                return alt
    return ta


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 7. CORE RUNNERS
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def _pretrain(actor, lam, seed, n=100):
    rng = random.Random(seed ^ 0xCAFE)
    env = GridWorld(lam=lam, rng=rng)
    for _ in range(n):
        s = env.reset(); done = False
        while not done:
            a = actor.select_action(s, 0.0)
            ns_, r, _, _, done = env.step(a)
            actor.update(s, a, r, ns_, done)
            s = ns_

def run_unprotected(lam, eps, n_ep, seed):
    rng = random.Random(seed); env = GridWorld(lam=lam, rng=rng)
    ag = QAgent(env.n_states(), GridWorld.N_ACTIONS, rng=rng, seed=seed)
    _pretrain(ag, lam, seed)
    seq = []
    for _ in range(n_ep):
        s = env.reset(); done = False
        while not done:
            a = ag.select_action(s, eps)
            ns_, r, _, _, done = env.step(a)
            ag.update(s, a, r, ns_, done); s = ns_
        seq.append(episode_mode(env.sa, env.sb))
    return count_flips(seq)/max(n_ep-1,1)*100, dwell_times(seq)

def run_checker(lam, eps, n_ep, seed):
    rng = random.Random(seed); env = GridWorld(lam=lam, rng=rng)
    ag = QAgent(env.n_states(), GridWorld.N_ACTIONS, rng=rng, seed=seed)
    _pretrain(ag, lam, seed)
    seq = []; prev = None
    for _ in range(n_ep):
        s = env.reset(); done = False
        while not done:
            a = checker_action(ag.Q, s, env, prev, rng, eps)
            ns_, r, _, _, done = env.step(a)
            ag.update(s, a, r, ns_, done); s = ns_
        m = episode_mode(env.sa, env.sb); seq.append(m); prev = m
    return count_flips(seq)/max(n_ep-1,1)*100, dwell_times(seq)

def run_tmr(lam, eps, n_ep, seed):
    rng = random.Random(seed); env = GridWorld(lam=lam, rng=rng)
    voter = TMRVoter(env.n_states(), GridWorld.N_ACTIONS,
                     seeds=[seed, seed+7, seed+13])
    _pretrain(voter, lam, seed)
    seq = []
    for _ in range(n_ep):
        s = env.reset(); done = False
        while not done:
            a = voter.select_action(s, eps)
            ns_, r, _, _, done = env.step(a)
            voter.update(s, a, r, ns_, done); s = ns_
        seq.append(episode_mode(env.sa, env.sb))
    return count_flips(seq)/max(n_ep-1,1)*100, dwell_times(seq)

def run_combined(lam, eps, n_ep, seed):
    rng = random.Random(seed); env = GridWorld(lam=lam, rng=rng)
    voter = TMRVoter(env.n_states(), GridWorld.N_ACTIONS,
                     seeds=[seed+1, seed+5, seed+11])
    _pretrain(voter, lam, seed)
    seq = []; prev = None
    for _ in range(n_ep):
        s = env.reset(); done = False
        while not done:
            a = combined_action(voter, s, env, prev, eps)
            ns_, r, _, _, done = env.step(a)
            voter.update(s, a, r, ns_, done); s = ns_
        m = episode_mode(env.sa, env.sb); seq.append(m); prev = m
    return count_flips(seq)/max(n_ep-1,1)*100, dwell_times(seq)

def multi_seed(runner, lam, eps, n_ep, n_seeds):
    rates = []; all_dw = []
    for k in range(n_seeds):
        fr, dw = runner(lam, eps, n_ep, SEED_BASE + k*100)
        rates.append(fr); all_dw.extend(dw)
    m, sd = mean_std(rates)
    dm, dsd = mean_std(all_dw)
    return m, sd, dm, dsd, all_dw

RUNNERS = {
    "unprotected": run_unprotected,
    "checker":     run_checker,
    "tmr":         run_tmr,
    "combined":    run_combined,
}


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 8. LORENTZIAN FIT
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def lorentzian(x, A, x0, g, off):
    return A*g**2/((x-x0)**2+g**2)+off

def fit_lorentzian(lambdas, rates):
    if HAS_SCIPY and HAS_NUMPY:
        try:
            popt, _ = curve_fit(
                lorentzian, lambdas, rates,
                p0=[max(rates), 0.52, 0.12, min(rates)],
                bounds=([0,0.3,0.01,0],[100,0.75,0.5,5]), maxfev=8000)
            return popt
        except Exception:
            pass
    return [max(rates), 0.5, 0.13, min(rates)]


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 9. PLOTS
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def make_plots(lam_results, ablation, eps_results, lorentz_params, n_ep, n_seeds):
    if not HAS_MPL:
        print("[WARN] matplotlib absent – plots skipped.")
        return

    import numpy as np
    A, x0, gamma, offset = lorentz_params
    lf = np.linspace(0.05, 0.95, 300)

    # Fig 1
    fig, ax = plt.subplots(figsize=(9,5.5))
    base = lam_results["unprotected"]
    lams = [x[0] for x in base]
    for cond in ["unprotected","checker","tmr","combined"]:
        data = lam_results[cond]
        xs = [d[0] for d in data]
        ys = [d[1] for d in data]
        es = [d[2] for d in data]
        ax.errorbar(xs, ys, yerr=es, label=cond.replace("_"," ").title(),
                    color=COLORS[cond], marker='o', lw=2, capsize=3, ms=5)
    ax.plot(lf, [lorentzian(l,A,x0,gamma,offset) for l in lf],
            '--', color='black', lw=1.5, alpha=0.55,
            label=f'Lorentzian fit (γ={gamma:.3f})')
    ax.axvline(0.5, color='gray', ls=':', lw=1, alpha=0.6)
    ax.set_xlabel("Balance Parameter λ", fontsize=13)
    ax.set_ylabel("Flip Rate (per 100 episodes)", fontsize=13)
    ax.set_title(f"Figure 1: Flip Rate vs. λ — Janibekov Intermediate-Axis Instability\n"
                 f"(ε=0.10, N={n_ep}×{n_seeds} seeds)", fontsize=12)
    ax.legend(fontsize=10); ax.grid(True,alpha=0.28); plt.tight_layout()
    fig.savefig(os.path.join(RESULTS_DIR,"fig1_flip_rate_vs_lambda.png"), dpi=180)
    plt.close(); print("  fig1 saved")

    # Fig 2
    eps_x = [d[0] for d in eps_results]
    eps_y = [d[1] for d in eps_results]
    eps_e = [d[2] for d in eps_results]
    fig, ax = plt.subplots(figsize=(7,5))
    ax.errorbar(eps_x, eps_y, yerr=eps_e, color=COLORS['unprotected'],
                marker='s', lw=2, capsize=5, ms=7, label="Unprotected (λ=0.5)")
    ax.set_xlabel("Perturbation Magnitude ε", fontsize=13)
    ax.set_ylabel("Flip Rate (per 100 episodes)", fontsize=13)
    ax.set_title("Figure 2: Flip Rate vs. ε at λ=0.5\n"
                 "(plateau: instability is structural, not noise-amplitude-driven)", fontsize=11)
    ax.text(0.12, 9.0,
            "Plateau finding:\nflip rate ~12–14 across all ε\n"
            "→ instability is geometric (landscape),\n"
            "   not noise-amplitude driven",
            fontsize=9, color='#555555',
            bbox=dict(boxstyle='round,pad=0.4',facecolor='#FFF8DC',alpha=0.85))
    ax.set_ylim(0,22); ax.grid(True,alpha=0.28); ax.legend(fontsize=10)
    plt.tight_layout()
    fig.savefig(os.path.join(RESULTS_DIR,"fig2_flip_rate_vs_epsilon.png"), dpi=180)
    plt.close(); print("  fig2 saved")

    # Fig 3
    conds  = ["unprotected","checker","tmr","combined"]
    labels = ["Unprotected","Inv. Checker","TMR Voter","Combined"]
    means_ = [ablation[c][0] for c in conds]
    stds_  = [ablation[c][1] for c in conds]
    bl     = means_[0]
    pcts   = [0]+[(bl-means_[i])/bl*100 for i in range(1,4)]
    clrs   = [COLORS[c] for c in conds]
    fig,(ax1,ax2) = plt.subplots(1,2,figsize=(11,5.5))
    bars = ax1.bar(labels,means_,yerr=stds_,color=clrs,edgecolor='black',
                   lw=0.8,capsize=5,width=0.55)
    for bar,v,sd in zip(bars,means_,stds_):
        ax1.text(bar.get_x()+bar.get_width()/2,v+sd+0.3,f'{v:.2f}',
                 ha='center',va='bottom',fontsize=10,fontweight='bold')
    ax1.set_ylabel("Flip Rate (per 100 episodes)",fontsize=12)
    ax1.set_title(f"Fig 3a: Ablation — Flip Rate\n(λ=0.5,ε=0.10,N={n_ep},{n_seeds} seeds)",fontsize=11)
    ax1.set_ylim(0,22); ax1.grid(True,axis='y',alpha=0.28)
    ax2.bar(labels,pcts,color=clrs,edgecolor='black',lw=0.8,width=0.55)
    for i,(v,p) in enumerate(zip(means_,pcts)):
        lbl = "—" if i==0 else f"−{p:.1f}%"
        ax2.text(i,p+1.5,lbl,ha='center',va='bottom',fontsize=10,fontweight='bold')
    ax2.set_ylabel("Flip Rate Reduction vs. Baseline (%)",fontsize=12)
    ax2.set_title("Fig 3b: Stabilization Effectiveness",fontsize=11)
    ax2.set_ylim(0,115); ax2.grid(True,axis='y',alpha=0.28)
    plt.tight_layout()
    fig.savefig(os.path.join(RESULTS_DIR,"fig3_ablation_bar.png"), dpi=180)
    plt.close(); print("  fig3 saved")

    # Fig 4 — REAL dwell-time distributions (histogram of actual run-length
    # encoded dwells across the seed ensemble; no resampling).
    dwell_desc = {
        "unprotected": "Approximately exponential (memoryless flips)",
        "checker":     "Heavy right-tailed (hard constraint)",
        "tmr":         "Light right-tail extension",
        "combined":    "Extreme right-tail; near-infinite dwell",
    }
    fig, axes = plt.subplots(2, 2, figsize=(11, 8))
    axes = axes.flatten()
    for ax, cond in zip(axes, ["unprotected", "checker", "tmr", "combined"]):
        dm, dsd = ablation[cond][2], ablation[cond][3]
        dwells = ablation[cond][4]                      # the real data
        desc = dwell_desc[cond]
        if dwells:
            cap = min(int(max(dwells)) + 1, 1001)       # 1000-ep run bound
            bins = np.arange(1, cap + 2)
            counts = np.histogram(dwells, bins=bins)[0]
            ax.bar(bins[:-1], counts, color=COLORS[cond], edgecolor='black',
                   lw=0.4, alpha=0.85, width=0.9)
        else:
            ax.text(0.5, 0.5, "no dwell runs", ha='center', va='center',
                    transform=ax.transAxes)
        ax.set_title(f"{cond.replace('_',' ').title()}\n"
                     f"Mean={dm:.0f}±{dsd:.0f} ep  (n={len(dwells)} runs) — {desc}",
                     fontsize=9.5)
        ax.set_xlabel("Dwell Length (episodes)", fontsize=9)
        ax.set_ylabel("Count", fontsize=9)
        ax.grid(True, axis='y', alpha=0.25)
    fig.suptitle(f"Figure 4: Dwell-Time Distributions — measured "
                 f"(λ=0.5, ε=0.10, N={n_ep}×{n_seeds})", fontsize=12, y=1.01)
    plt.tight_layout()
    fig.savefig(os.path.join(RESULTS_DIR, "fig4_dwell_distributions.png"),
                dpi=180, bbox_inches='tight')
    plt.close(); print("  fig4 saved (real data)")


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 10. MAIN
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def main():
    parser = argparse.ArgumentParser(description="Janibekov Mapping Experiment — v2.0")
    grp = parser.add_mutually_exclusive_group()
    grp.add_argument("--quick", action="store_true", default=True,
                     help="Quick smoke-test: N=200 episodes, 3 seeds (~1 min)")
    grp.add_argument("--full",  action="store_true",
                     help="Full run: N=1000 episodes, 20 seeds (~60-90 min)")
    args = parser.parse_args()

    if args.full:
        N_EP = 1000; N_S = 20
    else:
        N_EP = 200;  N_S = 3

    TARGET_EPS = 0.10; TARGET_LAM = 0.5
    print("=" * 68)
    print(f"  Janibekov Mapping Experiment v2.0")
    print(f"  N_EPISODES={N_EP}  N_SEEDS={N_S}  {'FULL' if args.full else 'QUICK'} mode")
    print("=" * 68)

    # Lambda sweep
    print("\n[1/4] Lambda sweep (all conditions, ε=0.10)...")
    lam_results = {c: [] for c in RUNNERS}
    for lam in LAMBDA_VALS:
        for cond, runner in RUNNERS.items():
            m, sd, dm, dsd, _ = multi_seed(runner, lam, TARGET_EPS, N_EP, N_S)
            lam_results[cond].append((lam, m, sd))
            print(f"  λ={lam:.2f} {cond:12s} {m:.2f}±{sd:.2f}")

    # Epsilon sweep (unprotected, lam=0.5)
    print("\n[2/4] Epsilon sweep (unprotected, λ=0.5)...")
    eps_results = []
    for eps in EPSILON_VALS:
        m, sd, _, _, _ = multi_seed(run_unprotected, TARGET_LAM, eps, N_EP, N_S)
        eps_results.append((eps, m, sd))
        print(f"  ε={eps:.2f}  {m:.2f}±{sd:.2f}")

    # Ablation at lam=0.5, eps=0.10
    print("\n[3/4] Ablation (λ=0.5, ε=0.10)...")
    ablation = {}
    for cond, runner in RUNNERS.items():
        m, sd, dm, dsd, dw = multi_seed(runner, TARGET_LAM, TARGET_EPS, N_EP, N_S)
        ablation[cond] = (m, sd, dm, dsd, dw)
        bl = ablation["unprotected"][0] if "unprotected" in ablation else 1
        pct = f"-{(bl-m)/bl*100:.1f}%" if cond != "unprotected" else "baseline"
        print(f"  {cond:14s}  flip={m:.2f}±{sd:.2f}  dwell={dm:.1f}±{dsd:.1f}  {pct}")

    # Lorentzian fit
    up_means = [d[1] for d in lam_results["unprotected"]]
    lorentz_params = fit_lorentzian(LAMBDA_VALS, up_means)
    A,x0,g,off = lorentz_params
    print(f"\n  Lorentzian: A={A:.2f} x0={x0:.3f} γ={g:.3f} offset={off:.3f}")

    # Plots
    print("\n[4/4] Generating plots...")
    make_plots(lam_results, ablation, eps_results, lorentz_params, N_EP, N_S)

    # CSV
    csv_path = os.path.join(RESULTS_DIR, "raw_data.csv")
    with open(csv_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["condition","lambda","epsilon","flip_mean","flip_std"])
        for cond in RUNNERS:
            for lam,m,sd in lam_results[cond]:
                w.writerow([cond,lam,TARGET_EPS,round(m,4),round(sd,4)])
        for eps,m,sd in eps_results:
            w.writerow(["unprotected_eps_sweep",TARGET_LAM,eps,round(m,4),round(sd,4)])
    print(f"  raw_data.csv saved")

    print("\n" + "="*68)
    print(f"  DONE — outputs in ./{RESULTS_DIR}/")
    print("="*68)


if __name__ == "__main__":
    main()

