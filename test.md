## [原始Markdown语法](https://daringfireball.net/projects/markdown/syntax)

最高阶标题
=======

第二阶标题
-----------

# 这是 H1 #

## 这是 H2 ##

#### 这是 H4
##### 这是 H5
###### 这是 H6

`行内代码`

> 引用(blockquote)，可嵌套

    缩进4个空格 原文输出(pre)

	缩进1个tab 原文输出(pre)

*强调* 或者 _强调_

**加重强调** 或者 __加重强调__

* 无序列表1
* 无序列表2
* 无序列表3
+ 无序列表4
- 无序列表5

1. 有序列表1
3. 有序列表2
6. 有序列表3

---
水平分割线
***

（-间加空格或单起一段）

<http://example.net/>    链接

[text](http://example.net/ "optional title")    链接

[text][label]    注释式链接

[label]: http://example.com/  "optional title" or 'title' or (title)

![text](http://example.com/example.png "optional title")    图片

![text](data:image/png;base64,字符串)

![text][]
[text](http://example.com/example.png "optional title")

$\alpha$

$$
独立公式
$$

# MultiMarkdown扩展语法

[^1]

[^1]: ...

{: #id .class key='values'}
[Link to id1](#id1)

| 表头1 | 表头2 | 表头3| 表头4 |
| ---- |:---- |:----:| ----:|
| 单元格 | 左对齐 | 居中  | 右对齐 |
| 单元格 | 左对齐 | 居中  | 右对齐 |

# [Github-flavoured Markdown扩展语法](https://developer.github.com/v3/markdown/)
~~删除线~~

^ 上标

- [] - [x] 复选框

```语言
代码块
```

~~~
代码块
~~~

也可以缩进 4 个空格或是 1 个制表符（Tab），输入代码


| 表头1 | 表头2 | 表头3 |
| --- | --- | --- |
| 左对齐 | 居中  | 右对齐 |
| 左对齐 | 居中  | 右对齐 |

# PHP Markdown Extra扩展语法


# [CommonMark语法](http://commonmark.org/)扩展语法


# Pandoc扩展语法
\换行符: 强制换行     *escaped\_line\_breaks*
标题的#后必须有空格     *blank\_before\_header*
缩进的>前必须有空行     *blank\_before\_blockquote*
多行代码块     *fenced\_code\_blocks*
"| "行区块

# [MyST](https://mystmd.org/)扩展语法

# [Ghost Markdown语法](https://help.ghost.org/hc/en-us/articles/224410728-Markdown-Guide)

# [Masterway(mmd)语法](https://masterway.cc/mmd)
无序列表支持. 不支持+
有序列表不需要手动输入序号，+自动编号
[], [y], [x] 复选框
Tab构造多级缩进
/video: /youtube: /bilibili: 视频

# 其他扩展语法
[居中]
[右对齐]]