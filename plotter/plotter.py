"""
Plotter for TFHE hardware scheduler:
- Engine-parallel Gantt charts
- Job-level CDF
- Job-level turnaround summary
"""

import csv
import sys
import os
import matplotlib.pyplot as plt
import numpy as np

# tikzplotlib optional
try:
    import tikzplotlib
    _HAS_TIKZ = True
except Exception:
    tikzplotlib = None
    _HAS_TIKZ = False


# =====================================================
# CSV LOADERS
# =====================================================

def load_job_csv(path):
    jobs = []
    with open(path, newline='') as f:
        reader = csv.DictReader(f)
        for r in reader:
            jobs.append({
                'job_id': int(r['job_id']),
                'tenant_id': int(r['tenant_id']),
                'arrival_us': float(r['arrival_us']),
                'start_us': float(r['start_us']),
                'completion_us': float(r['completion_us']),
                'num_bootstraps': int(r.get('num_bootstraps', 0)),
                'key_size_mb': float(r.get('key_size_mb', 0.0)),
                'pcie_transferred': int(float(r.get('pcie_transferred', 0)))
            })
    return jobs


def load_engine_csv(path):
    """
    Columns: engine, job_id, start_us, end_us
    """
    events = []
    with open(path, newline='') as f:
        reader = csv.DictReader(f)
        for r in reader:
            events.append({
                'engine': int(r['engine']),
                'job_id': int(r['job_id']),
                'start_us': float(r['start_us']),
                'end_us': float(r['end_us'])
            })
    return events


# =====================================================
# PLOTTING HELPERS
# =====================================================

def plot_engine_gantt(ax, events, title):
    """
    Real multi-engine Gantt plot.
    One horizontal lane per engine.
    """
    if not events:
        ax.set_title(title + " (no events)")
        return

    engines = sorted(set(e['engine'] for e in events))
    job_ids = sorted(set(e['job_id'] for e in events))

    # consistent colors by job-id
    cmap = plt.get_cmap('tab20')
    colors = {jid: cmap(jid % 20) for jid in job_ids}

    for e in engines:
        lane_events = [ev for ev in events if ev['engine'] == e]
        for ev in lane_events:
            ax.barh(
                e,
                ev['end_us'] - ev['start_us'],
                left=ev['start_us'],
                color=colors[ev['job_id']],
                edgecolor='black',
                linewidth=0.3
            )

    ax.set_yticks(engines)
    ax.set_yticklabels([f"Eng {e}" for e in engines])
    ax.set_xlabel("Time (us)")
    ax.set_title(title)
    ax.invert_yaxis()


def plot_cdf(ax, jobs, label):
    comp = np.array([j['completion_us'] for j in jobs])
    comp_sorted = np.sort(comp)
    probs = np.arange(1, len(comp_sorted) + 1) / float(len(comp_sorted))
    ax.plot(comp_sorted, probs, label=label)
    ax.set_xlabel("Completion time (us)")
    ax.set_ylabel("CDF")
    ax.grid(True)


def summary_stats(jobs):
    turnarounds = [j['completion_us'] - j['arrival_us'] for j in jobs]
    return float(np.mean(turnarounds)), float(np.median(turnarounds))

def plot_job_gantt(ax, events, title):
    """
    Job-level Gantt: each job is a lane, and every engine slice is a segment.
    events = engine-level log: engine, job_id, start_us, end_us
    """
    if not events:
        ax.set_title(title + " (no events)")
        return

    job_ids = sorted(set(ev['job_id'] for ev in events))
    cmap = plt.get_cmap('tab20')
    colors = {jid: cmap(jid % 20) for jid in job_ids}

    # One lane per job (0..n-1)
    y_positions = {jid: i for i, jid in enumerate(job_ids)}

    for ev in events:
        jid = ev['job_id']
        ax.barh(
            y_positions[jid],
            ev['end_us'] - ev['start_us'],
            left=ev['start_us'],
            color=colors[jid],
            edgecolor='black',
            linewidth=0.3
        )

    # ax.set_yticks(list(y_positions.values()))
    # ax.set_yticklabels([f"{jid}" for jid in job_ids]) # Job
    # ax.invert_yaxis()
    # ax.set_xlabel("Time (us)")
    # ax.set_title(title)

    n_jobs = len(job_ids)
    step = 5  # Show every 10 jobs;
    yticks = list(range(0, n_jobs, step))
    yticklabels = [str(job_ids[i]) for i in yticks]

    ax.set_yticks(yticks)
    ax.set_yticklabels(yticklabels)
    ax.set_ylabel("Process ID")

    ax.invert_yaxis()
    ax.set_xlabel("Time (us)")
    ax.set_title(title)

def plot_job_span(ax, jobs, title):
    """
    Classic job-level Gantt: one bar per job from start_us to completion_us.
    """
    jobs_sorted = sorted(jobs, key=lambda j: j['job_id'])
    job_ids = [j['job_id'] for j in jobs_sorted]
    starts = [j['start_us'] for j in jobs_sorted]
    lengths = [j['completion_us'] - j['start_us'] for j in jobs_sorted]

    cmap = plt.get_cmap('tab20')
    colors = {jid: cmap(jid % 20) for jid in job_ids}

    ax.barh(
        job_ids,
        lengths,
        left=starts,
        color=[colors[jid] for jid in job_ids],
        edgecolor='black',
        linewidth=0.3
    )
    ax.set_xlabel("Time (us)")
    ax.set_ylabel("Job ID")
    ax.set_title(title)
    ax.invert_yaxis()



# =====================================================
# MAIN
# =====================================================

def main():
    args = sys.argv[1:]
    if len(args) < 1:
        print("Usage: python3 plotter.py <prefix> [--out-prefix X]")
        print("Expected files:")
        print("  <prefix>-fifo.csv")
        print("  <prefix>-fifo-engines.csv")
        print("  <prefix>-hps.csv")
        print("  <prefix>-hps-engines.csv")
        sys.exit(1)

    prefix = args[0]
    out_prefix = prefix

    if '--out-prefix' in args:
        i = args.index('--out-prefix')
        if i + 1 < len(args):
            out_prefix = args[i + 1]

    # Expected input files
    fifo_jobs_path = f"{prefix}-fifo.csv"
    fifo_eng_path  = f"{prefix}-fifo-engines.csv"
    hps_jobs_path  = f"{prefix}-hps.csv"
    hps_eng_path   = f"{prefix}-hps-engines.csv"

    for p in [fifo_jobs_path, fifo_eng_path, hps_jobs_path, hps_eng_path]:
        if not os.path.exists(p):
            print("ERROR: Missing file:", p)
            sys.exit(1)

    fifo_jobs = load_job_csv(fifo_jobs_path)
    fifo_engs = load_engine_csv(fifo_eng_path)
    hps_jobs  = load_job_csv(hps_jobs_path)
    hps_engs  = load_engine_csv(hps_eng_path)

    # =====================================================
    #  FIGURE: parallel Gantt + CDF
    # =====================================================

    fig = plt.figure(figsize=(14, 12))
    gs = fig.add_gridspec(3, 2, height_ratios=[2, 2, 1])

    ax_f_gantt = fig.add_subplot(gs[0, :])
    ax_h_gantt = fig.add_subplot(gs[1, :])
    ax_cdf     = fig.add_subplot(gs[2, :])

    plot_engine_gantt(ax_f_gantt, fifo_engs, "FIFO: Engine-level Execution")
    plot_engine_gantt(ax_h_gantt, hps_engs, "HPS: Engine-level Execution")
    plot_cdf(ax_cdf, fifo_jobs, "FIFO")
    plot_cdf(ax_cdf, hps_jobs,  "HPS")
    ax_cdf.legend()

    plt.tight_layout()
    out_file = f"{out_prefix}_gantt_engines_cdf.png"
    fig.savefig(out_file, dpi=250)
    print("Wrote", out_file)

    # if _HAS_TIKZ:
    #     try:
    #         tikzplotlib.save(f"{out_prefix}_gantt_engines_cdf.tex")
    #         print("Wrote", f"{out_prefix}_gantt_engines_cdf.tex")
    #     except Exception as e:
    #         print("tikzplotlib failed:", e)

    # =====================================================
    #  Summary
    # =====================================================

    # mean_fifo, med_fifo = summary_stats(fifo_jobs)
    # mean_hps, med_hps = summary_stats(hps_jobs)

    # fig2, ax2 = plt.subplots(figsize=(6, 4))
    # labels = ["FIFO", "HPS"]
    # means = [mean_fifo, mean_hps]
    # meds  = [med_fifo, med_hps]

    # x = np.arange(len(labels))
    # width = 0.35
    # ax2.bar(x - width/2, means, width, label="Mean turnaround")
    # ax2.bar(x + width/2, meds,  width, label="Median turnaround")

    # ax2.set_xticks(x)
    # ax2.set_xticklabels(labels)
    # ax2.set_ylabel("Time (us)")
    # ax2.set_title("Turnaround Summary")
    # ax2.legend()

    # fig2.tight_layout()
    # out_file2 = f"{out_prefix}_turnaround_summary.png"
    # fig2.savefig(out_file2, dpi=250)
    # print("Wrote", out_file2)

    # if _HAS_TIKZ:
    #     try:
    #         tikzplotlib.save(f"{out_prefix}_turnaround_summary.tex")
    #         print("Wrote", f"{out_prefix}_turnaround_summary.tex")
    #     except Exception as e:
    #         print("tikzplotlib failed:", e)

    # ================================================
    # JOB-LEVEL GANTT (both FIFO and HPS)
    # ================================================
    fig3 = plt.figure(figsize=(14, 6))
    gs3 = fig3.add_gridspec(2, 1)

    ax_job_fifo = fig3.add_subplot(gs3[0, 0])
    ax_job_hps  = fig3.add_subplot(gs3[1, 0])

    plot_job_gantt(ax_job_fifo, fifo_engs, "FIFO: Process-Level Execution (Engine Slices)")
    plot_job_gantt(ax_job_hps,  hps_engs,  "HPS: Process-Level Execution (Engine Slices)")

    plt.tight_layout()
    out_file3 = f"{out_prefix}_job_gantt_slices.png"
    fig3.savefig(out_file3, dpi=250)
    print("Wrote", out_file3)

    # if _HAS_TIKZ:
    #     try:
    #         tikzplotlib.save(f"{out_prefix}_job_gantt_slices.tex")
    #         print("Wrote", f"{out_prefix}_job_gantt_slices.tex")
    #     except Exception as e:
    #         print("tikzplotlib failed:", e)

    # fig4 = plt.figure(figsize=(14, 12))
    # gs4 = fig4.add_gridspec(2, 1)

    # ax_span_fifo = fig4.add_subplot(gs4[0, 0])
    # ax_span_hps  = fig4.add_subplot(gs4[1, 0])

    # plot_job_span(ax_span_fifo, fifo_jobs, "FIFO: Job-Level Span (Start → Completion)")
    # plot_job_span(ax_span_hps,  hps_jobs,  "HPS: Job-Level Span (Start → Completion)")

    # plt.tight_layout()
    # out_file4 = f"{out_prefix}_job_gantt_spans.png"
    # fig4.savefig(out_file4, dpi=250)
    # print("Wrote", out_file4)

    # if _HAS_TIKZ:
    #     try:
    #         tikzplotlib.save(f"{out_prefix}_job_gantt_spans.tex")
    #         print("Wrote", f"{out_prefix}_job_gantt_spans.tex")
    #     except Exception as e:
    #         print("tikzplotlib failed:", e)


if __name__ == "__main__":
    main()
