#!/usr/bin/env python3

import argparse
import math
import sys
from pathlib import Path

FIELDS = ("%B", "f_value", "queries", "memory_mb_est", "running_time_ms")


def load_plotting_library():
    try:
        import matplotlib.pyplot as plt
        import matplotlib.ticker as mticker
        import numpy as np
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "chưa cài matplotlib. Hãy chạy: python3 -m pip install matplotlib"
        ) from exc
    return plt, mticker, np


def parse_data(filename):
    path = Path(filename)
    if not path.is_file():
        raise FileNotFoundError(f"Không tìm thấy file '{path}'")

    data = {
        "FOCUS": {field: [] for field in FIELDS},
        "OFFLINE": {field: [] for field in FIELDS},
    }
    current_algo = None

    with path.open("r", encoding="utf-8") as handle:
        for line_number, raw_line in enumerate(handle, start=1):
            line = raw_line.strip()
            if not line:
                continue
            if line in data:
                current_algo = line
                continue
            if line.startswith("%B"):
                continue
            if current_algo is None:
                raise ValueError(
                    f"Dòng {line_number}: cần có FOCUS hoặc OFFLINE trước dữ liệu"
                )

            parts = line.split("\t")
            if len(parts) not in (6, 7):
                parts = line.split()
            if len(parts) not in (6, 7):
                raise ValueError(
                    f"Dòng {line_number}: cần 6 hoặc 7 cột %B, B, eval_f_value, queries, "
                    "memory_mb_est, running_time_ms[, violation]"
                )

            try:
                percent_b = float(parts[0].removesuffix("%"))
                f_value = float(parts[2])
                queries = float(parts[3])
                memory = float(parts[4])
                running_time = float(parts[5])
            except ValueError as exc:
                raise ValueError(f"Dữ liệu số không hợp lệ tại dòng {line_number}") from exc

            values = (percent_b, f_value, queries, memory, running_time)
            for field, value in zip(FIELDS, values):
                data[current_algo][field].append(value)

    for method, method_data in data.items():
        if not method_data["%B"]:
            raise ValueError(f"Không có dữ liệu cho thuật toán {method}")
        rows = sorted(
            zip(*(method_data[field] for field in FIELDS)),
            key=lambda row: row[0],
        )
        for index, field in enumerate(FIELDS):
            method_data[field] = [row[index] for row in rows]

    return data


def get_nice_max(max_value):
    if max_value <= 0:
        return 1.0
    raw_step = max_value * 1.1 / 4
    exponent = math.floor(math.log10(raw_step))
    fraction = raw_step / (10**exponent)
    nice_fraction = min(
        (1.0, 2.0, 2.5, 5.0, 7.5, 10.0),
        key=lambda value: abs(value - fraction),
    )
    return nice_fraction * (10**exponent) * 4


def draw_chart(parsed_data, y_field, y_label, title, filename, plt, mticker, np):
    # Giữ nguyên style của script ban đầu.
    plt.rcParams['font.family'] = 'sans-serif'
    plt.rcParams['font.size'] = 12
    plt.figure(figsize=(8, 6))
    plt.subplots_adjust(left=0.18, right=0.95, bottom=0.15, top=0.82)

    plot_styles = {
        "FOCUS": {
            "marker": "o",
            "color": "tab:red",
            "label": "FOCUS_RR",
        },
        "OFFLINE": {
            "marker": "^",
            "color": "tab:blue",
            "label": "Offline_Greedy_CC",
        },
    }

    all_y_values = []
    all_x_values = []
    for method, method_data in parsed_data.items():
        style = plot_styles[method]
        y_values = method_data[y_field]
        z_order = 10 if method == 'FOCUS' else 3
        plt.plot(
            method_data['%B'],
            y_values,
            marker=style["marker"],
            linestyle='-',
            color=style["color"],
            label=style["label"],
            markersize=15,
            markeredgecolor=style["color"],
            fillstyle='none',
            linewidth=1.8 if method == 'FOCUS' else 1.5,
            zorder=z_order,
            clip_on=False,
        )
        all_x_values.extend(method_data['%B'])
        all_y_values.extend(y_values)

    legend_handles = []
    for method_name in ['FOCUS', 'OFFLINE']:
        style = plot_styles[method_name]
        legend_handles.append(
            plt.Line2D(
                [], [],
                marker=style['marker'],
                color=style['color'],
                linestyle='None',
                markersize=10,
                markeredgecolor=style['color'],
                fillstyle='none',
                label=style['label'],
            )
        )

    plt.title(title, fontsize=24, pad=75)
    plt.legend(
        handles=legend_handles,
        loc='upper center',
        bbox_to_anchor=(0.5, 1.17),
        ncol=2,
        fontsize=15,
        frameon=False,
        handletextpad=0.5,
        columnspacing=2.0,
        markerscale=1.6,
    )

    plt.xlabel('%B', fontsize=14, labelpad=10)
    plt.ylabel(y_label, fontsize=14, labelpad=10)

    x_values = sorted(set(all_x_values))
    if x_values:
        plt.xlim(min(x_values) - 0.5, max(x_values) + 0.5)
        plt.xticks(x_values)

    if all_y_values:
        max_y = get_nice_max(max(all_y_values) * 1.1)
        plt.ylim(0, max_y)

    def y_formatter(value, _position):
        if value >= 1000:
            value_k = value / 1000
            if value_k % 1 == 0:
                return f'{int(value_k)}K'
            return f'{value_k:.1f}K'
        return f'{value:.0f}'

    plt.yticks(np.linspace(*plt.ylim(), 5))
    plt.gca().yaxis.set_major_formatter(mticker.FuncFormatter(y_formatter))
    plt.tick_params(axis='x', length=0, pad=15)
    plt.tick_params(axis='y', length=0)

    plt.grid(False)
    for y_value in plt.gca().get_yticks():
        plt.axhline(y=y_value, color='grey', linestyle=':', linewidth=0.7)
    for x_value in x_values:
        plt.plot(
            [x_value, x_value],
            [plt.ylim()[0], plt.ylim()[1]],
            color='grey',
            linestyle=':',
            linewidth=0.7,
        )

    for spine in ['top', 'right', 'bottom', 'left']:
        plt.gca().spines[spine].set_visible(False)

    plt.savefig(filename, dpi=300, bbox_inches='tight')
    plt.close()


def main():
    parser = argparse.ArgumentParser(
        description="Vẽ biểu đồ so sánh FOCUS và OFFLINE."
    )
    parser.add_argument(
        "data_file",
        nargs="?",
        default="results/facebook.txt",
        help="File chứa hai khối FOCUS và OFFLINE",
    )
    arguments = parser.parse_args()

    data_file = Path(arguments.data_file)
    try:
        parsed_data = parse_data(data_file)
        plt, mticker, np = load_plotting_library()
    except (FileNotFoundError, RuntimeError, ValueError) as exc:
        print(f"Lỗi: {exc}", file=sys.stderr)
        return 1

    output_prefix = data_file.with_suffix("")
    chart_specs = (
        ("f_value", "Objective Value f", "Objective Value"),
        ("queries", "Number of Oracle Queries", "Oracle Query Count"),
        ("memory_mb_est", "Estimated Memory Usage (MiB)", "Estimated Memory Usage"),
        ("running_time_ms", "Running Time (ms)", "Running Time"),
    )

    for field, label, title in chart_specs:
        output_file = output_prefix.parent / f"{output_prefix.name}_{field}.pdf"
        draw_chart(parsed_data, field, label, title, output_file, plt, mticker, np)
        print(f"Đã tạo: {output_file}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
