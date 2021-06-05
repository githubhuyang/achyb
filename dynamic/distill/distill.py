import os
import re
import sys
import numpy as np
import random
from matplotlib import pyplot as plt
from sklearn.cluster import KMeans
from sklearn.cluster import DBSCAN
from sklearn.cluster import AgglomerativeClustering
from scipy.cluster.hierarchy import dendrogram
from sklearn.metrics import silhouette_samples, silhouette_score, calinski_harabasz_score, davies_bouldin_score


def parse_dir(dir_path):
    t_lst = []
    for file_name in sorted(os.listdir(dir_path)):
        file_path = dir_path + "/" + file_name

        fs = os.path.getsize(file_path)

        with open(file_path, "r") as f:
            m_lst = []
            c_lst = []

            need_cover = False
            for line in f:
                if "\"Cover:" in line and need_cover:
                    c = parse_cover(line)
                    c_lst.append(c)
                    need_cover = False
                else:
                    m = parse_record(line)
                    if m is not None:
                        m_lst.append(m)
                        need_cover = True

            assert(len(m_lst) >= len(c_lst))
            for i in range(len(m_lst)):
                if i < len(c_lst):
                    m_lst[i]["cov"] = c_lst[i]
                else:
                    m_lst[i]["cov"] = []

            t_lst.append(m_lst)
    return t_lst


def parse_cover(line):
    cover_info = line[7:].strip()
    return cover_info.split(",")


#arg_re_str = r"(\d{1,}|\w{1,}|\{.*\}|[.*])"
re_str = r"^\d+\s+(\w+)\((.*)\)\s+=\s+([\w\-\?]+).*\n$"
record_re = re.compile(re_str)
def parse_record(line):
    m = {}
    match = record_re.match(line)
    if match:
        g_lst = match.groups()
        if len(g_lst) == 3:
            # print(g_lst)
            m["ori"] = line
            m["call"] = g_lst[0]
            m["ret"] = g_lst[2]
            m["args"] = parse_args(g_lst[1])
            return m
        else:
            print("Illegal record")
            print(len(g_lst))
            for g in g_lst:
                print(g)
            exit(0)
    elif "exited with" in line:
        return None
    elif "killed by" in line:
        return None

    return None


def get_last_idx(s, b, d):
    assert(s[0] == b)
    cnt = 0
    for i in range(len(s)):
        if s[i] == b:
            cnt += 1
        elif s[i] == d:
            cnt -= 1

        if cnt == 0:
            return i
    return None


re_arg_str = r"([\w\|\\]+|" + \
             r"\"[\w\|\\]+\"|" + \
             r"\w+\=[\|\w\\]+|" + \
             r"\w+\=\"[\|\w\\]+\"|" + \
             r"[\&\w]+\=[\|\w\\]+|" + \
             r"[\&\w]+\=\"[\|\w\\]+\")"
#re_args_str = r"^" + re_arg_str + "{0,1}" + r"(,\s+" + re_arg_str + ")*$"
arg_re = re.compile(re_arg_str)
def parse_args(args_str):
    args_str = args_str.strip()

    ng_lst = []
    if len(args_str) == 0:
        return ng_lst
    elif args_str[0] == ",":
        sub_g_lst = parse_args(args_str[1:].strip())
        if sub_g_lst is not None:
            ng_lst.extend(sub_g_lst)
    elif args_str[0] == "[":
        pos = get_last_idx(args_str, "[", "]")
        # print(g[1:-1].strip())
        if pos is not None:
            sub_g_lst = parse_args(args_str[1:pos].strip())
            if sub_g_lst is not None:
                ng_lst.extend(sub_g_lst)

            sub_g_lst = parse_args(args_str[pos+1:].strip())
            if sub_g_lst is not None:
                ng_lst.extend(sub_g_lst)

    elif args_str[0] == "{":
        pos = get_last_idx(args_str, "{", "}")
        if pos is not None:
            sub_g_lst = parse_args(args_str[1:pos].strip())
            if sub_g_lst is not None:
                ng_lst.extend(sub_g_lst)

            sub_g_lst = parse_args(args_str[pos+1:].strip())
            if sub_g_lst is not None:
                ng_lst.extend(sub_g_lst)
    else:
        match = arg_re.match(args_str)
        if match:
            g = match.groups()[0]
            ng_lst.append(g)

            sub_g_lst = parse_args(args_str[len(g):].strip())
            if sub_g_lst is not None:
                ng_lst.extend(sub_g_lst)

    return ng_lst


trace2node = {}
ele2node = {}
node_cnt = 0
def get_node(ele, is_trace=False):
    global node_cnt

    if ele in ele2node:
        return ele2node[ele]
    else:
        node_id = node_cnt
        node_cnt += 1
        ele2node[ele] = node_id
        if is_trace:
            trace2node[ele] = node_id
        return node_id


def gen_graph(t_lst, graph_path):
    edge_cnt = 0

    tc_set = set()
    with open(graph_path, "w") as f:
        for m_lst in t_lst:
            # for w in range(10, 11): #range(len(m_lst), len(m_lst)+1):
            assert(len(m_lst) > 0)

            trace = "".join(m["ori"] for m in m_lst)
            t_node = get_node(trace, is_trace=True)
            
            r_idx = 0
            for m in m_lst: # m_lst[:w]
                record = m["ori"]
                r_node = get_node(record)
                rlt = "rdx" + str(r_idx)
                r_idx += 1

                # trace to record
                edge = (t_node, rlt, r_node)
                f.write("\t".join(str(e) for e in edge) + "\n")
                edge_cnt += 1

                syscall = m["call"]
                syscall_node = get_node(syscall)
                rlt = "syscall"
                # record to syscall
                edge = (r_node, rlt, syscall_node)
                f.write("\t".join(str(e) for e in edge) + "\n")
                edge_cnt += 1

                # adx = 0
                for arg in m["args"]:
                    a_node = get_node(arg)
                    rlt = "arg" # + str(adx)
                    # adx += 1

                    # record to args
                    edge = (r_node, rlt, a_node)
                    f.write("\t".join(str(e) for e in edge) + "\n")
                    edge_cnt += 1

                ret = m["ret"]
                ret_node = get_node(ret)
                rlt = "ret"

                # record to return
                edge = (r_node, rlt, ret_node)
                f.write("\t".join(str(e) for e in edge) + "\n")
                edge_cnt += 1
                
                for cov in m["cov"]:
                    cov_node = get_node(cov)

                    tc = str(t_node) + " " + str(cov_node)
                    if tc not in tc_set:
                        edge = (t_node, "cov", cov_node)
                        f.write("\t".join(str(e) for e in edge) + "\n")
                        edge_cnt += 1
                        tc_set.add(tc)

    print("total edges: ", edge_cnt)

    return trace2node


if __name__ == "__main__":
    sys.setrecursionlimit(100000)
    tst_dir_path = os.getcwd() + "/traces/sim-ltp-traces"

    print("parse traces")
    # t_lst = parse_dir(exp_dir_path)
    t_lst = parse_dir(tst_dir_path)

    print("# trace record: ", sum(len(m_lst) for m_lst in t_lst))

    print("generate graph")
    data_dir = os.getcwd() + "/data"
    if not os.path.isdir(data_dir):
        os.makedirs(data_dir)

    graph_path = os.getcwd() + "/data/raw_graph.txt"
    gen_graph(t_lst, graph_path)