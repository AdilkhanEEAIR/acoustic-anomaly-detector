import argparse
import glob
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd
from sklearn.metrics import (
    ConfusionMatrixDisplay,
    accuracy_score,
    classification_report,
)
from sklearn.model_selection import train_test_split
from sklearn.tree import DecisionTreeClassifier, plot_tree

FEATURES = ["rms", "p2p", "crest_x100", "zcr", "mad", "diff_rms"]


def load_data(folder: str) -> pd.DataFrame:
    files = sorted(glob.glob(f"{folder}/*.csv"))
    if not files:
        raise SystemExit(
            f"No CSV files in {folder}/. Record data with logger.py first."
        )
    frames = []
    for i, path in enumerate(files):
        df = pd.read_csv(path)
        df["session"] = i
        frames.append(df)
    data = pd.concat(frames, ignore_index=True)
    print(f"Loaded {len(data)} windows from {len(files)} files")
    print(data["label"].value_counts().to_string(), "\n")
    return data


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default="data")
    parser.add_argument("--out", default="results")
    parser.add_argument("--binary", action="store_true", help="normal vs anomaly")
    parser.add_argument(
        "--max-depth",
        type=int,
        default=4,
        help="small depth = small C code on the chip",
    )
    args = parser.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    data = load_data(args.data)
    if args.binary:
        data["label"] = data["label"].where(data["label"] == "normal", "anomaly")

    X = data[FEATURES].values
    y = data["label"].values

    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.25, random_state=42, stratify=y
    )

    model = DecisionTreeClassifier(max_depth=args.max_depth, random_state=42)
    model.fit(X_train, y_train)
    y_pred = model.predict(X_test)

    acc = accuracy_score(y_test, y_pred)
    report = classification_report(y_test, y_pred, digits=3)
    print(f"Test accuracy: {acc:.3f}\n")
    print(report)

    importances = sorted(zip(FEATURES, model.feature_importances_), key=lambda t: -t[1])
    print("Feature importance:")
    for name, imp in importances:
        print(f"  {name:12s} {imp:.3f}")

    (out / "metrics.json").write_text(
        json.dumps(
            {
                "accuracy": round(float(acc), 4),
                "n_train": len(y_train),
                "n_test": len(y_test),
                "max_depth": args.max_depth,
                "tree_depth": int(model.get_depth()),
                "tree_leaves": int(model.get_n_leaves()),
                "feature_importance": {k: round(float(v), 4) for k, v in importances},
            },
            indent=2,
        )
    )
    (out / "report.txt").write_text(f"Test accuracy: {acc:.3f}\n\n{report}")

    fig, ax = plt.subplots(figsize=(5, 4.5))
    ConfusionMatrixDisplay.from_predictions(
        y_test, y_pred, ax=ax, cmap="Blues", colorbar=False
    )
    ax.set_title(f"Confusion matrix (accuracy {acc:.1%})")
    fig.tight_layout()
    fig.savefig(out / "confusion_matrix.png", dpi=150)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(14, 7))
    plot_tree(
        model,
        feature_names=FEATURES,
        class_names=list(model.classes_),
        filled=True,
        rounded=True,
        fontsize=8,
        ax=ax,
    )
    fig.tight_layout()
    fig.savefig(out / "decision_tree.png", dpi=150)
    plt.close(fig)

    fig, axes = plt.subplots(2, 3, figsize=(12, 7))
    labels = sorted(data["label"].unique())
    for ax, feat in zip(axes.flat, FEATURES):
        ax.boxplot([data.loc[data["label"] == lb, feat] for lb in labels])
        ax.set_xticks(range(1, len(labels) + 1))
        ax.set_xticklabels(labels)
        ax.set_title(feat)
        ax.tick_params(axis="x", rotation=30)
    fig.suptitle("Time-domain features by class")
    fig.tight_layout()
    fig.savefig(out / "features_by_class.png", dpi=150)
    plt.close(fig)

    try:
        from micromlgen import port

        (out / "model.h").write_text(
            port(model, classmap={i: c for i, c in enumerate(model.classes_)})
        )
        print(f"\nC code for the chip: {out / 'model.h'}")
    except ImportError:
        print("\nmicromlgen not installed - skip C export (pip install micromlgen)")

    print(f"Pictures and metrics saved in {out}/")


if __name__ == "__main__":
    main()
