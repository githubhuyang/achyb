# ACHyb: A Hybrid Analysis Approach to Detect Kernel Access Control Vulnerabilities
[![DOI](https://zenodo.org/badge/371857467.svg)](https://zenodo.org/badge/latestdoi/371857467)

## Intro

In the paper, we conduct an empirical study on Kernel Access Control Vulnerabilities (KACVs) using National Vulnerability Database. Motivated by our study, we focus on detecting two kinds of KACVs: KACV-M and KACV-I. In particular, we present a precise and scalable hybrid analysis approach called `ACHyb` to detect both KACV-M and KACV-I. ACHyb first performs a more precise and more sound static analysis to identify the potentially vulnerable paths, and then applies an efficient dynamic analysis to reduce the false positives of the detected potential paths.

In this repo, we release four software tools developed in our work: 1) [a cve analysis tool](cve-analyzer) to conduct our KACV study 2) [a static analysis tool](static) to detect potentially vulnerable paths, 3) [a clustering-base seed distillation tool](dynamic/distill) to generate high-quality seed programs, and 4) a [kernel fuzzer](dynamic/fuzzing) to reduce false positives of the potential paths reported our static analysis tool. For each tool, we document setup procedures and usage, and provide the corresponding datasets.

We got two badges from the Artifact Track of ESEC/FSE'21: [Artifacts Evaluated – Reusable](https://www.acm.org/publications/policies/artifact-review-and-badging-current#reusable) badge and [Artifacts Available](https://www.acm.org/publications/policies/artifact-review-and-badging-current#available) badge. 

## Authors

Yang Hu, Wenxi Wang, Casen Hunger, Riley Wood, Sarfraz Khurshid, Mohit Tiwari

## Publication

If you use any part of our tool or dataset present in this repository, please kindly cite our paper.

```tex
@inproceedings{yang2021fse,
    title={ACHyb: A Hybrid Analysis Approach to Detect Kernel Access Control Vulnerabilities},
    author={Hu, Yang and Wang, Wenxi and Hunger, Casen and Wood, Riley and Khurshid, Sarfraz and Tiwari, Mohit},
    booktitle={Proceedings of the 29th ACM Joint European Software Engineering Conference and Symposium on the Foundations of Software Engineering (ESEC/FSE'21)},
    pages={316--327},
    year={2021},
    organization={ACM}
}
```

## Repo Structure

We arrange the source code of the four software tools in the following directory tree:

```
|-cve-analyzer	
|-static
|-dynamic
	|-distill
	|-fuzzing
```

The`cve-analyzer` folder contains the source code of our cve analysis tool; The `static` folders contains the source code of our static analysis tool; The `dynamic/distill` contains the source code of our seed distillation tool; The `dynamic/fuzzing` folder contains the source code of our fuzzing tool.

## VirtualBox Image

For the ease of using our system, we provide a [VirtualBox image](https://drive.google.com/file/d/1_8cNLm3-jq9BX7zGh1UmUSnj2zMjybet/view?usp=sharing) with complete experimental setup. Please load the image via VirtualBox 6.1 or later version with the `Nested VT-x/AMD-V` feature enabled. The username and password for login are `guest`. Source files are saved in the `/home/guest` folder. The same image is available on [Zenodo](https://doi.org/10.5281/zenodo.5083754) as well.

## Other Links

#### [Install/Usage](INSTALL.md)

#### [Requirements](REQUIREMENTS.md)

## License

[MIT License](LICENSE)
