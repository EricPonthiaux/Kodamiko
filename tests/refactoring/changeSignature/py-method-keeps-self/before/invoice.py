class Invoice:
    def §total(self, tax):
        return tax


def bill(invoice: Invoice):
    return invoice.total(0.2)
