import os
import pandas as pd
import matplotlib.pyplot as plt

def get_peak_to_peak_voltage(filepath):
    """Reads the CSV and calculates the Peak-to-Peak voltage."""
    try:
        df = pd.read_csv(filepath)
        if df.shape[1] < 2:
            return None

        # Grab the Voltage column (column 2)
        voltage = pd.to_numeric(df.iloc[:, 1], errors="coerce").dropna()
        
        if voltage.empty:
            return None
            
        # Peak-to-peak is the absolute max minus the absolute min
        v_p2p = voltage.max() - voltage.min()
        return v_p2p

    except Exception as e:
        print(f"❌ Error reading {filepath}: {e}")
        return None

# 1. Define the folder where your CSVs live
DATA_FOLDER = "jely_pump_1MHz"
script_dir = os.path.dirname(os.path.abspath(__file__))
data_dir = os.path.join(script_dir, DATA_FOLDER)

# 2. Map the X-axis (Stroke Number) to the actual file names
# X=0 is the Baseline (No Stroke)
stroke_mapping = {
    0: "no stroke .csv",
    1: "1st stroke.csv",
    2: "2nd stroke.csv",
    3: "3rd stroke.csv",
    4: "4th stroke.csv",
    5: "5th stroke.csv",
    6: "6th stroke.csv",
    7: "7th stroke.csv",
    8: "8th stroke.csv",
    9: "9th stroke.csv",
    10: "10th stroke.csv"
}

# Lists to hold our final, clean X and Y data
x_strokes = []
y_voltage = []

print("Analyzing data...")

# 3. Loop through files and extract just the voltage amplitude
for stroke_num, filename in stroke_mapping.items():
    full_path = os.path.join(data_dir, filename)
    
    if os.path.exists(full_path):
        v_p2p = get_peak_to_peak_voltage(full_path)
        if v_p2p is not None:
            x_strokes.append(stroke_num)
            y_voltage.append(v_p2p)
            print(f"Stroke {stroke_num}: {v_p2p:.3f} V")
    else:
        print(f"⚠️ File missing: {filename}")

# 4. Plot the Clean Attenuation Curve
plt.figure(figsize=(10, 6))

# Plot the line with clear circular markers at each data point
plt.plot(x_strokes, y_voltage, marker='o', linestyle='-', color='#1f77b4', 
         linewidth=2, markersize=8, markerfacecolor='white', markeredgewidth=2, label="Measured Attenuation")

# Format the Graph for a professional report
plt.title('Gelfoam Slurry Concentration vs. Pump Strokes (1 MHz)', fontsize=14, fontweight='bold')
plt.xlabel('Number of Pump Strokes', fontsize=12, fontweight='bold')
plt.ylabel('Peak-to-Peak Received Voltage (V)', fontsize=12, fontweight='bold')

# Ensure X-axis only shows whole numbers (you can't have 1.5 strokes)
plt.xticks(range(0, 11))

plt.grid(True, which='both', linestyle='--', alpha=0.6)
plt.legend(loc='upper right', fontsize=11)
plt.tight_layout()

# Save the output image
output_path = os.path.join(data_dir, 'attenuation_curve_output.png')
plt.savefig(output_path, dpi=300) # dpi=300 makes it super crisp for Word/PDF reports
print(f"\n✅ Clean curve saved to: {output_path}")

print("Opening graph...")
plt.show()