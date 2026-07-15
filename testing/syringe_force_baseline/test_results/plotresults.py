import pandas as pd
import matplotlib.pyplot as plt
from scipy.signal import find_peaks

filename = "test_20260709_123600 (baseline lyostypt).csv"

# Read data
df = pd.read_csv(filename)

x = df["ms"]
y = df["grams"]

# Find local maxima
peaks, _ = find_peaks(
    y,
    height=200,
    prominence=0.3
)

# Peak coordinates
peak_x = x.iloc[peaks]
peak_y = y.iloc[peaks]

# Statistics
max_idx = peak_y.idxmax()
min_idx = peak_y.idxmin()

max_peak = peak_y.max()
min_peak = peak_y.min()
avg_peak = peak_y.mean()

print(f"Maximum Peak: {max_peak:.4f} g")
print(f"Minimum Peak: {min_peak:.4f} g")
print(f"Average Peak: {avg_peak:.4f} g")

# Plot
plt.figure(figsize=(10, 5))
plt.plot(x, y, label="Mass")
plt.scatter(peak_x, peak_y, color="red", s=30, label="Local Maxima")

# Highlight max and min peaks
plt.scatter(x.loc[max_idx], max_peak, color="blue", s=120, marker="^", label="Maximum Peak")
plt.scatter(x.loc[min_idx], min_peak, color="green", s=120, marker="v", label="Minimum Peak")

# Horizontal average line
plt.axhline(avg_peak, color="purple", linestyle="--",
            label=f"Average Peak = {avg_peak:.2f} g")

# Annotate values
plt.annotate(f"Max: {max_peak:.2f} g",
             (x.loc[max_idx], max_peak),
             xytext=(10, 10),
             textcoords="offset points")

plt.annotate(f"Min: {min_peak:.2f} g",
             (x.loc[min_idx], min_peak),
             xytext=(10, -20),
             textcoords="offset points")

plt.xlabel("Time (ms)")
plt.ylabel("Mass (grams)")
plt.title("Mass vs Time")
plt.grid(True)
plt.legend()

plt.show()