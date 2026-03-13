import matplotlib
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt
import re
import sys
import hashlib

def get_color(addr):
    """Generates a consistent hex color based on the region address."""
    hash_object = hashlib.md5(addr.encode())
    return "#" + hash_object.hexdigest()[:6]

def main():
    if len(sys.argv) < 2:
        print("Usage: python viz_verona.py <log_file>")
        sys.exit(1)

    log_filename = sys.argv[1]
    
    # Regex: 1=Timestamp, 2=Action, 3=Address, 4=GC tag
    pattern = re.compile(r"(\d+\.\d+): (OPEN|CLOSE) REGION \((0x[0-9a-f]+)\)( GC)?")
    
    intervals = []
    active_opens = {}  # (address, is_gc) -> start_time
    gc_lanes = {}      # lane_idx -> last_end_time
    
    def get_gc_lane(start_time):
        lane = 1
        while True:
            if gc_lanes.get(lane, 0) <= start_time:
                return lane
            lane += 1

    try:
        with open(log_filename, 'r') as f:
            for line in f:
                match = pattern.search(line)
                if not match:
                    continue
                
                ts, action, addr, is_gc_str = match.groups()
                ts = float(ts)
                is_gc = is_gc_str is not None
                state_key = (addr, is_gc)

                if action == "OPEN":
                    active_opens[state_key] = ts
                elif action == "CLOSE" and state_key in active_opens:
                    start_ts = active_opens.pop(state_key)
                    
                    if not is_gc:
                        lane = 0  # Mutator always on Lane 0
                    else:
                        lane = get_gc_lane(start_ts)
                        gc_lanes[lane] = ts
                        
                    intervals.append((lane, start_ts, ts, addr, is_gc))

    except FileNotFoundError:
        print(f"Error: File '{log_filename}' not found.")
        sys.exit(1)

    # --- Plotting ---
    fig, ax = plt.subplots(figsize=(14, 7))
    
    for lane, start, end, addr, is_gc in intervals:
        duration = end - start
        color = get_color(addr)
        hatch = '///' if is_gc else None
        
        ax.barh(lane, duration, left=start, height=0.6, 
                color=color, edgecolor='black', alpha=0.7, hatch=hatch)
        
        # Label with last 4 chars of hex for readability
        #if duration > 0.002: 
         #'   ax.text(start + duration/2, lane, addr[-4:], 
           #         ha='center', va='center', fontsize=7, fontweight='bold')

    # Visual formatting
    max_lane = max([i[0] for i in intervals]) if intervals else 1
    ax.set_yticks(range(max_lane + 1))
    ax.set_yticklabels(['Mutator'] + [f'GC Stream {i}' for i in range(1, max_lane + 1)])
    
    ax.set_xlabel('Time (Seconds)')
    ax.set_title(f'Verona Region Lifecycle: {log_filename}')
    ax.grid(axis='x', linestyle='--', alpha=0.5)

    # Custom Legend
    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor='grey', label='Mutator Work'),
        Patch(facecolor='grey', hatch='///', label='GC Operation'),
        Patch(facecolor='white', edgecolor='black', label='Colors = Unique Regions')
    ]
    ax.legend(handles=legend_elements, loc='upper right')

    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    main()