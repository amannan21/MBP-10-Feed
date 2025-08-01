import pandas as pd


if __name__ == "__main__":
    keep_cols = [c for c in pd.read_csv("mbp_new.csv", nrows=0).columns]  # your header
    orig = pd.read_csv("mbp.csv",      usecols=keep_cols, nrows=100)
    new  = pd.read_csv("mbp_new.csv",  usecols=keep_cols, nrows=100)

    print("Rows identical in MBP slice:", (orig == new).all(axis=1).sum(), "/ 100")
