ARCOL: Aspect-Ratio-Controlled Orthogonal Layout

ARCOL is an extension of the HOLA (Hierarchical Orthogonal Layout Algorithm) implemented in the DiAlEcT layout framework https://github.com/mjwybrow/adaptagrams.
This project introduces an aspect-ratio control mechanism that produces orthogonal graph layouts adapted for arbitrary target shapes (e.g., 1:1, 16:9, 9:16, A4 portrait/landscape).

Unlike existing orthogonal layout methods, ARCOL integrates a soft aspect ratio constraint directly inside the stress minimization phase of HOLA, allowing the layout to gradually converge to a user-defined aspect ratio without breaking orthogonality, compaction, or readability.
