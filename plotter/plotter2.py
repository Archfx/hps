
"""
Minimal, focused plotter for FIFO vs HPS CSV comparison.

Saves PNG outputs and attempts to save TikZ .tex if tikzplotlib is available and
working. If tikzplotlib import fails (common when matplotlib backend mismatch),
the script will still produce PNGs and print an instructional message.
"""

import csv
import sys
import os
import matplotlib.pyplot as plt
import numpy as np

# Try to import tikzplotlib but don't fail if unavailable/broken.
try:
    import tikzplotlib
    _HAS_TIKZ = True
except Exception:
    tikzplotlib = None
    _HAS_TIKZ = False


def load_csv(path):
    jobs = []
    with open(path, newline='') as f:
        reader = csv.DictReader(f)
        for r in reader:
            job = {
                'job_id': int(r['job_id']),
                'tenant_id': int(r['tenant_id']),
                'arrival_us': float(r['arrival_us']),
                'start_us': float(r['start_us']),
                'completion_us': float(r['completion_us']),
                'num_bootstraps': int(r.get('num_bootstraps', 0)),
                'key_size_mb': float(r.get('key_size_mb', 0.0)),
                'pcie_transferred': int(float(r.get('pcie_transferred', 0)))
            }
            jobs.append(job)
    return jobs


def plot_gantt(ax, jobs, title):
    jobs_sorted = sorted(jobs, key=lambda j: (j['start_us'], j['job_id']))
    starts = [j['start_us'] for j in jobs_sorted]
    lengths = [max(1.0, j['completion_us'] - j['start_us']) for j in jobs_sorted]
    tenant_colors = {}
    cmap = plt.get_cmap('tab10')
    colors = []
    nc = 0
    for j in jobs_sorted:
        t = j['tenant_id']
        if t not in tenant_colors:
            tenant_colors[t] = cmap(nc % 10)
            nc += 1
        colors.append(tenant_colors[t])

    ax.barh(range(len(jobs_sorted)), lengths, left=starts, color=colors, edgecolor='k')
    ax.set_yticks(range(len(jobs_sorted)))
    ax.set_yticklabels([str(j['job_id']) for j in jobs_sorted])
    ax.invert_yaxis()
    ax.set_xlabel('Time (us)')
    ax.set_title(title)


def plot_cdf(ax, jobs, label):
    comp = np.array([j['completion_us'] for j in jobs])
    comp_sorted = np.sort(comp)
    probs = np.arange(1, len(comp_sorted) + 1) / float(len(comp_sorted))
    ax.plot(comp_sorted, probs, label=label)
    ax.set_xlabel('Completion time (us)')
    ax.set_ylabel('CDF')
    ax.grid(True)


def summary_stats(jobs):
    turnarounds = [j['completion_us'] - j['arrival_us'] for j in jobs]
    return float(np.mean(turnarounds)), float(np.median(turnarounds))


def main():
    args = sys.argv[1:]
    out_prefix = 'compare'
    if len(args) == 0:
        fifo_path = 'test_small_fifo.csv'
        hps_path = 'test_small_hps.csv'
    elif len(args) >= 2:
        fifo_path = args[0]
        hps_path = args[1]
        if '--out-prefix' in args:
            i = args.index('--out-prefix')
            if i + 1 < len(args):
                out_prefix = args[i + 1]
    else:
        print('Usage: python3 plotter.py fifo.csv hps.csv [--out-prefix PREFIX]')
        sys.exit(1)

    if not (os.path.exists(fifo_path) and os.path.exists(hps_path)):
        print('One or both CSV inputs missing:', fifo_path, hps_path)
        sys.exit(1)

    fifo_jobs = load_csv(fifo_path)
    hps_jobs = load_csv(hps_path)

    fig = plt.figure(figsize=(14, 8))
    gs = fig.add_gridspec(2, 2, height_ratios=[2, 1])
    ax_g1 = fig.add_subplot(gs[0, 0])
    ax_g2 = fig.add_subplot(gs[0, 1])
    ax_cdf = fig.add_subplot(gs[1, :])

    plot_gantt(ax_g1, fifo_jobs, 'FIFO: job start -> completion')
    plot_gantt(ax_g2, hps_jobs, 'HPS: job start -> completion')
    plot_cdf(ax_cdf, fifo_jobs, 'FIFO')
    plot_cdf(ax_cdf, hps_jobs, 'HPS')
    ax_cdf.legend()

    plt.tight_layout()
    out_file = f"{out_prefix}_compare_gantt_cdf.png"
    fig.savefig(out_file, dpi=250)
    print('Wrote', out_file)

    if _HAS_TIKZ:
        try:
            tikzplotlib.save(f"{out_prefix}_compare_gantt_cdf.tex")
            print('Wrote', f"{out_prefix}_compare_gantt_cdf.tex")
        except Exception as e:
            print('tikzplotlib failed to save .tex (trying legend-free fallback):', e)
            try:
                # Remove legends from all axes and try again to avoid backend/legend internals issues
                for ax in fig.axes:
                    legend = ax.get_legend()
                    if legend is not None:
                        legend.remove()
                tikzplotlib.save(f"{out_prefix}_compare_gantt_cdf_no_legend.tex")
                print('Wrote', f"{out_prefix}_compare_gantt_cdf_no_legend.tex")
            except Exception as e2:
                print('legend-free tikz export also failed:', e2)
    else:
        print('tikzplotlib unavailable — to enable .tex export: pip3 install tikzplotlib')

    mean_fifo, med_fifo = summary_stats(fifo_jobs)
    mean_hps, med_hps = summary_stats(hps_jobs)
    labels = ['FIFO', 'HPS']
    means = [mean_fifo, mean_hps]
    meds = [med_fifo, med_hps]

    fig2, ax2 = plt.subplots(1, 1, figsize=(6, 4))
    x = np.arange(len(labels))
    width = 0.35
    ax2.bar(x - width/2, means, width, label='Mean turnaround (us)')
    ax2.bar(x + width/2, meds, width, label='Median turnaround (us)')
    ax2.set_xticks(x)
    ax2.set_xticklabels(labels)
    ax2.set_ylabel('Time (us)')
    ax2.set_title('Scheduler turnaround summary')
    ax2.legend()
    fig2.tight_layout()
    out_file2 = f"{out_prefix}_summary.png"
    fig2.savefig(out_file2, dpi=200)
    print('Wrote', out_file2)

    if _HAS_TIKZ:
        try:
            tikzplotlib.save(f"{out_prefix}_summary.tex")
            print('Wrote', f"{out_prefix}_summary.tex")
        except Exception as e:
            print('tikzplotlib failed to save .tex (trying legend-free fallback):', e)
            try:
                for ax in fig2.axes:
                    legend = ax.get_legend()
                    if legend is not None:
                        legend.remove()
                tikzplotlib.save(f"{out_prefix}_summary_no_legend.tex")
                print('Wrote', f"{out_prefix}_summary_no_legend.tex")
            except Exception as e2:
                print('legend-free tikz export also failed:', e2)
    else:
        print('tikzplotlib unavailable — to enable .tex export: pip3 install tikzplotlib')


if __name__ == '__main__':
    main()
