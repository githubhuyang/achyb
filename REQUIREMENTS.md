# Requirements (Experimental Environment)

## Hardware Environment

`ACHyb` is able to run on a machine with at least 4 logical cores and 16GB RAM. However, we would suggest using a machine with more cores and larger RAM, as it helps to accelerate the dynamic analysis. 

## Software Environment

In general, we assume that all the four tools of `ACHyb` run on the Ubuntu 19.04 OS or later versions. For the tools which include `Python` programs, users should install `Python 3.6` or later versions. We suggest using the [Anaconda 3](https://www.anaconda.com/products/individual) package distribution, which includes most of required packages. Besides, each tool has its specific software dependencies. We will document them separately. Please refer to the `README.md` for each tool.

#### [README for the cve analysis tool](cve-analyzer/README.md)

#### [README for the static analysis tool](static/README.md)

#### [README for the seed distillation tool](dynamic/distill/README.md)

#### [README for the fuzzing tool](dynamic/fuzzing/README.md)







