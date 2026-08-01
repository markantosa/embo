import pandas as pd
import matplotlib.pyplot as plt

filename = "uas_vpp_data.csv"

# Read data
df = pd.read_csv(filename)

x = df["stroke"]
y = df["vpp"]

# Attenuation relative to stroke 0 baseline (dB)
baseline = y.iloc[0]
attenuation_db = 20 * (y / baseline).apply(lambda v: 0 if v <= 0 else __import__("math").log10(v))

print(f"Baseline VPP (stroke 0): {baseline:.3f} V")
print(f"Final VPP (stroke {x.iloc[-1]}): {y.iloc[-1]:.3f} V")
print(f"Total attenuation: {attenuation_db.iloc[-1]:.2f} dB")

# Plot raw VPP vs stroke
fig, ax1 = plt.subplots(figsize=(10, 5))
ax1.plot(x, y, marker="o", color="tab:blue", label="VPP (V)")
ax1.set_xlabel("Stroke count")
ax1.set_ylabel("VPP (V)", color="tab:blue")
ax1.tick_params(axis="y", labelcolor="tab:blue")
ax1.grid(True)

# Overlay attenuation (dB) on secondary axis
ax2 = ax1.twinx()
ax2.plot(x, attenuation_db, marker="s", color="tab:red", linestyle="--", label="Attenuation (dB)")
ax2.set_ylabel("Attenuation (dB)", color="tab:red")
ax2.tick_params(axis="y", labelcolor="tab:red")

plt.title("UAS Signal vs Stroke Count")
fig.tight_layout()
plt.savefig("uas_vpp_plot.png")
plt.show()
