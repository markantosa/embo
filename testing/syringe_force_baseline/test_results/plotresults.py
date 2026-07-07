import pandas as pd
import matplotlib.pyplot as plt

df= pd.read_csv("test_20260707_163140.csv")

plt.figure(figsize=(10, 5))
plt.plot(df["ms"], df["grams"], linewidth=2)

# Labels
plt.xlabel("Time (ms)")
plt.ylabel("Weight (grams)")
plt.title("Weight vs Time")

# Grid
plt.grid(True)

# Show
plt.show()

plt.savefig("graph.png", dpi=300)