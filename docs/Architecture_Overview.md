Architecture Overview
The Smart Resource Scheduler is a complex OS-level application designed to optimize CPU and memory usage on Linux systems. Key components include:

Scheduler: Hybrid Priority and Round-Robin scheduling with adaptive quantum adjustments.
Process Manager: Manages processes with cgroups, CPU affinity, and real-time profiling.
Memory Manager: Simulates zswap compression and predictive allocation.
IPC Manager: Uses message queues for scheduler-UI communication.
UI: Qt-based dashboard with heatmaps and predictive indicators.
Synchronization: Semaphores and thread pools for thread safety.
Logging: JSON-based performance analytics with variance tracking.

The system uses JSON configurations for mode-specific settings and integrates with Linux kernel interfaces for advanced resource management.